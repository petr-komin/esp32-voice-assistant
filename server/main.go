// Command server is the voice service for the ESP32 voice terminal.
//
// For now it is an echo service: the audio a device sends is returned unchanged.
// The device protocol is described in docs/protocol.md.
package main

import (
	"context"
	"crypto/subtle"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/coder/websocket"
)

const (
	sampleRate     = 16000
	frameBytes     = sampleRate / 10 * 2 // 100 ms of 16-bit mono audio
	maxAudioBytes  = 30 * sampleRate * 2 // 30 s per utterance
	maxMessageSize = 64 * 1024
	helloTimeout   = 5 * time.Second
	idleTimeout    = 120 * time.Second // matches proxy_read_timeout in nginx
	authFailDelay  = time.Second       // slows down token guessing
	shutdownGrace  = 10 * time.Second
)

// message is a control message carried in a text frame. Each type uses a subset of the fields.
type message struct {
	Type   string         `json:"type"`
	Device string         `json:"device,omitempty"`
	Token  string         `json:"token,omitempty"`
	FW     string         `json:"fw,omitempty"`
	Format string         `json:"format,omitempty"`
	Rate   int            `json:"rate,omitempty"`
	Code   string         `json:"code,omitempty"`
	Action map[string]any `json:"action,omitempty"`
}

type server struct {
	token     string
	recordDir string // when set, every utterance is saved here as WAV
	log       *slog.Logger
}

// protocolError is reported to the device as {"type":"error"} before the connection is closed.
type protocolError struct {
	code   string
	detail string
}

func (e *protocolError) Error() string { return e.code + ": " + e.detail }

func (s *server) handleVoice(w http.ResponseWriter, r *http.Request) {
	c, err := websocket.Accept(w, r, nil)
	if err != nil {
		return
	}
	defer c.CloseNow()
	c.SetReadLimit(maxMessageSize)

	remote := r.Header.Get("X-Real-IP")
	if remote == "" {
		remote = r.RemoteAddr
	}
	log := s.log.With("remote", remote)

	err = s.session(r.Context(), c, log)
	var pe *protocolError
	switch {
	case err == nil, websocket.CloseStatus(err) == websocket.StatusNormalClosure,
		websocket.CloseStatus(err) == websocket.StatusGoingAway:
		log.Info("connection closed")
		c.Close(websocket.StatusNormalClosure, "")
	case errors.As(err, &pe):
		log.Warn("protocol error", "code", pe.code, "detail", pe.detail)
		if pe.code == "auth" {
			time.Sleep(authFailDelay)
		}
		_ = writeJSON(r.Context(), c, message{Type: "error", Code: pe.code})
		c.Close(websocket.StatusPolicyViolation, pe.code)
	default:
		log.Warn("connection lost", "err", err)
	}
}

func (s *server) session(ctx context.Context, c *websocket.Conn, log *slog.Logger) error {
	hello, err := readHello(ctx, c)
	if err != nil {
		return err
	}
	if subtle.ConstantTimeCompare([]byte(hello.Token), []byte(s.token)) != 1 {
		return &protocolError{"auth", "invalid token from device " + hello.Device}
	}
	log = log.With("device", hello.Device, "fw", hello.FW)
	log.Info("device connected")

	var audio []byte
	recording := false
	var startedAt time.Time

	for {
		readCtx, cancel := context.WithTimeout(ctx, idleTimeout)
		typ, data, err := c.Read(readCtx)
		cancel()
		if err != nil {
			return err
		}

		if typ == websocket.MessageBinary {
			if !recording {
				return &protocolError{"protocol", "audio outside start/end"}
			}
			if len(audio)+len(data) > maxAudioBytes {
				return &protocolError{"too_long", "utterance longer than 30 s"}
			}
			audio = append(audio, data...)
			continue
		}

		var msg message
		if err := json.Unmarshal(data, &msg); err != nil {
			return &protocolError{"protocol", "malformed JSON"}
		}
		switch msg.Type {
		case "start":
			if msg.Format != "pcm16" || msg.Rate != sampleRate {
				return &protocolError{"format", fmt.Sprintf("unsupported %s/%d", msg.Format, msg.Rate)}
			}
			audio = audio[:0]
			recording = true
			startedAt = time.Now()
			if err := writeJSON(ctx, c, message{Type: "ack"}); err != nil {
				return err
			}
		case "end":
			if !recording {
				return &protocolError{"protocol", "end without start"}
			}
			recording = false
			log.Info("utterance received",
				"bytes", len(audio),
				"seconds", float64(len(audio))/2/sampleRate,
				"took", time.Since(startedAt).Round(time.Millisecond))
			if err := writeJSON(ctx, c, message{Type: "thinking"}); err != nil {
				return err
			}
			s.save(audio, hello.Device, log)
			if err := s.respond(ctx, c, audio); err != nil {
				return err
			}
		default:
			return &protocolError{"protocol", "unknown message type " + msg.Type}
		}
	}
}

func readHello(ctx context.Context, c *websocket.Conn) (message, error) {
	ctx, cancel := context.WithTimeout(ctx, helloTimeout)
	defer cancel()
	typ, data, err := c.Read(ctx)
	if err != nil {
		return message{}, err
	}
	var msg message
	if typ != websocket.MessageText || json.Unmarshal(data, &msg) != nil || msg.Type != "hello" {
		return message{}, &protocolError{"protocol", "first message must be hello"}
	}
	return msg, nil
}

// respond sends the reply. For now it is the received audio, unchanged.
func (s *server) respond(ctx context.Context, c *websocket.Conn, audio []byte) error {
	if err := writeJSON(ctx, c, message{Type: "audio_start", Format: "pcm16", Rate: sampleRate}); err != nil {
		return err
	}
	for start := 0; start < len(audio); start += frameBytes {
		end := min(start+frameBytes, len(audio))
		if err := c.Write(ctx, websocket.MessageBinary, audio[start:end]); err != nil {
			return err
		}
	}
	return writeJSON(ctx, c, message{Type: "done", Action: map[string]any{"name": "echo", "bytes": len(audio)}})
}

func writeJSON(ctx context.Context, c *websocket.Conn, msg message) error {
	data, err := json.Marshal(msg)
	if err != nil {
		return err
	}
	return c.Write(ctx, websocket.MessageText, data)
}

func (s *server) save(audio []byte, device string, log *slog.Logger) {
	if s.recordDir == "" {
		return
	}
	// filepath.Base keeps a hostile device name from escaping recordDir.
	name := filepath.Join(s.recordDir,
		fmt.Sprintf("%s_%s.wav", time.Now().Format("2006-01-02_15-04-05"), filepath.Base(device)))
	if err := os.WriteFile(name, wav(audio), 0o640); err != nil {
		log.Warn("cannot save recording", "err", err)
		return
	}
	log.Info("recording saved", "file", name)
}

// wav wraps 16-bit mono PCM in a WAV header so it can be played in any player.
func wav(pcm []byte) []byte {
	h := make([]byte, 44, 44+len(pcm))
	copy(h[0:], "RIFF")
	binary.LittleEndian.PutUint32(h[4:], uint32(36+len(pcm)))
	copy(h[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(h[16:], 16) // fmt chunk size
	binary.LittleEndian.PutUint16(h[20:], 1)  // PCM
	binary.LittleEndian.PutUint16(h[22:], 1)  // mono
	binary.LittleEndian.PutUint32(h[24:], sampleRate)
	binary.LittleEndian.PutUint32(h[28:], sampleRate*2) // byte rate
	binary.LittleEndian.PutUint16(h[32:], 2)            // block align
	binary.LittleEndian.PutUint16(h[34:], 16)           // bits per sample
	copy(h[36:], "data")
	binary.LittleEndian.PutUint32(h[40:], uint32(len(pcm)))
	return append(h, pcm...)
}

func (s *server) mux() *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /voice", s.handleVoice)
	mux.HandleFunc("GET /health", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte("ok\n"))
	})
	return mux
}

func main() {
	log := slog.New(slog.NewJSONHandler(os.Stdout, nil))

	s := &server{
		token:     os.Getenv("VOICE_TOKEN"),
		recordDir: os.Getenv("RECORD_DIR"),
		log:       log,
	}
	if len(s.token) < 16 {
		log.Error("VOICE_TOKEN must be set and at least 16 characters long")
		os.Exit(1)
	}
	addr := os.Getenv("LISTEN_ADDR")
	if addr == "" {
		addr = ":8090"
	}

	srv := &http.Server{
		Addr:              addr,
		Handler:           s.mux(),
		ReadHeaderTimeout: 5 * time.Second,
		MaxHeaderBytes:    16 * 1024,
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	go func() {
		<-ctx.Done()
		log.Info("shutting down")
		shutdownCtx, cancel := context.WithTimeout(context.Background(), shutdownGrace)
		defer cancel()
		_ = srv.Shutdown(shutdownCtx)
	}()

	log.Info("listening", "addr", addr, "record_dir", s.recordDir)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Error("server failed", "err", err)
		os.Exit(1)
	}
}
