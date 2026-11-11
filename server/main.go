// Hlasová služba mluvící krabičky. Zatím jen echo: co krabička pošle, to vrátí.
// Protokol je popsaný v README.md v kořeni repozitáře.
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
	"path/filepath"
	"time"

	"github.com/coder/websocket"
)

const (
	sampleRate    = 16000
	frameBytes    = sampleRate / 10 * 2 // 100 ms audia, 16 bit mono
	maxAudioBytes = 30 * sampleRate * 2 // 30 s
	helloTimeout  = 5 * time.Second
	idleTimeout   = 120 * time.Second // stejně jako proxy_read_timeout v nginx
)

// message je řídicí zpráva v textovém rámci. Každý typ používá jen některá pole.
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
	recordDir string // když není prázdný, ukládá se sem každá nahrávka jako WAV
	log       *slog.Logger
}

// protocolError se pošle krabičce jako {"type":"error"} a spojení se zavře.
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
	c.SetReadLimit(64 * 1024)

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
		log.Info("spojení ukončeno")
		c.Close(websocket.StatusNormalClosure, "")
	case errors.As(err, &pe):
		log.Warn("chyba protokolu", "code", pe.code, "detail", pe.detail)
		writeJSON(r.Context(), c, message{Type: "error", Code: pe.code})
		c.Close(websocket.StatusPolicyViolation, pe.code)
	default:
		log.Warn("spojení spadlo", "err", err)
	}
}

func (s *server) session(ctx context.Context, c *websocket.Conn, log *slog.Logger) error {
	hello, err := readHello(ctx, c)
	if err != nil {
		return err
	}
	if subtle.ConstantTimeCompare([]byte(hello.Token), []byte(s.token)) != 1 {
		return &protocolError{"auth", "špatný token od " + hello.Device}
	}
	log = log.With("device", hello.Device, "fw", hello.FW)
	log.Info("krabička připojena")

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
				return &protocolError{"protocol", "audio mimo start/end"}
			}
			if len(audio)+len(data) > maxAudioBytes {
				return &protocolError{"too_long", "nahrávka delší než 30 s"}
			}
			audio = append(audio, data...)
			continue
		}

		var msg message
		if err := json.Unmarshal(data, &msg); err != nil {
			return &protocolError{"protocol", "nečitelný JSON"}
		}
		switch msg.Type {
		case "start":
			if msg.Format != "pcm16" || msg.Rate != sampleRate {
				return &protocolError{"format", fmt.Sprintf("%s/%d", msg.Format, msg.Rate)}
			}
			audio = audio[:0]
			recording = true
			startedAt = time.Now()
			if err := writeJSON(ctx, c, message{Type: "ack"}); err != nil {
				return err
			}
		case "end":
			if !recording {
				return &protocolError{"protocol", "end bez start"}
			}
			recording = false
			log.Info("nahrávka přijata",
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
			return &protocolError{"protocol", "neznámý typ " + msg.Type}
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
		return message{}, &protocolError{"protocol", "první zpráva musí být hello"}
	}
	return msg, nil
}

// respond pošle odpověď. Zatím je to přijaté audio beze změny.
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
	name := filepath.Join(s.recordDir,
		fmt.Sprintf("%s_%s.wav", time.Now().Format("2006-01-02_15-04-05"), filepath.Base(device)))
	if err := os.WriteFile(name, wav(audio), 0o644); err != nil {
		log.Warn("nahrávku nejde uložit", "err", err)
		return
	}
	log.Info("nahrávka uložena", "file", name)
}

// wav přidá k 16bitovému mono PCM hlavičku, aby šlo nahrávku pustit v přehrávači.
func wav(pcm []byte) []byte {
	h := make([]byte, 44, 44+len(pcm))
	copy(h[0:], "RIFF")
	binary.LittleEndian.PutUint32(h[4:], uint32(36+len(pcm)))
	copy(h[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(h[16:], 16) // délka bloku fmt
	binary.LittleEndian.PutUint16(h[20:], 1)  // PCM
	binary.LittleEndian.PutUint16(h[22:], 1)  // mono
	binary.LittleEndian.PutUint32(h[24:], sampleRate)
	binary.LittleEndian.PutUint32(h[28:], sampleRate*2) // bajtů za sekundu
	binary.LittleEndian.PutUint16(h[32:], 2)            // bajtů na vzorek
	binary.LittleEndian.PutUint16(h[34:], 16)           // bitů na vzorek
	copy(h[36:], "data")
	binary.LittleEndian.PutUint32(h[40:], uint32(len(pcm)))
	return append(h, pcm...)
}

func (s *server) mux() *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("/voice", s.handleVoice)
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("ok\n"))
	})
	return mux
}

func main() {
	log := slog.New(slog.NewTextHandler(os.Stdout, nil))

	s := &server{
		token:     os.Getenv("VOICE_TOKEN"),
		recordDir: os.Getenv("RECORD_DIR"),
		log:       log,
	}
	if s.token == "" {
		log.Error("chybí proměnná VOICE_TOKEN")
		os.Exit(1)
	}
	addr := os.Getenv("LISTEN_ADDR")
	if addr == "" {
		addr = ":8090"
	}

	log.Info("poslouchám", "addr", addr, "record_dir", s.recordDir)
	if err := http.ListenAndServe(addr, s.mux()); err != nil {
		log.Error("server skončil", "err", err)
		os.Exit(1)
	}
}
