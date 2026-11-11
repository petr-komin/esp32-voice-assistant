package main

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"log/slog"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func dial(t *testing.T) (context.Context, *websocket.Conn) {
	t.Helper()
	s := &server{token: "tajne", log: slog.New(slog.NewTextHandler(io.Discard, nil))}
	ts := httptest.NewServer(s.mux())
	t.Cleanup(ts.Close)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	t.Cleanup(cancel)
	c, _, err := websocket.Dial(ctx, "ws"+strings.TrimPrefix(ts.URL, "http")+"/voice", nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.CloseNow() })
	return ctx, c
}

func send(t *testing.T, ctx context.Context, c *websocket.Conn, msg message) {
	t.Helper()
	if err := writeJSON(ctx, c, msg); err != nil {
		t.Fatal(err)
	}
}

func expect(t *testing.T, ctx context.Context, c *websocket.Conn, want string) message {
	t.Helper()
	typ, data, err := c.Read(ctx)
	if err != nil {
		t.Fatalf("čekal jsem %q: %v", want, err)
	}
	var msg message
	if typ != websocket.MessageText || json.Unmarshal(data, &msg) != nil || msg.Type != want {
		t.Fatalf("čekal jsem %q, přišlo %s", want, data)
	}
	return msg
}

func TestEcho(t *testing.T) {
	ctx, c := dial(t)
	send(t, ctx, c, message{Type: "hello", Device: "test", Token: "tajne", FW: "0.1"})

	// Dvě kola za sebou, ať je vidět, že se buffer mezi nimi vyprázdní.
	for round := 0; round < 2; round++ {
		send(t, ctx, c, message{Type: "start", Format: "pcm16", Rate: 16000})
		expect(t, ctx, c, "ack")

		audio := make([]byte, 3*frameBytes+123)
		for i := range audio {
			audio[i] = byte(i*7 + round)
		}
		for start := 0; start < len(audio); start += 1000 {
			end := min(start+1000, len(audio))
			if err := c.Write(ctx, websocket.MessageBinary, audio[start:end]); err != nil {
				t.Fatal(err)
			}
		}
		send(t, ctx, c, message{Type: "end"})
		expect(t, ctx, c, "thinking")
		expect(t, ctx, c, "audio_start")

		var got []byte
		for {
			typ, data, err := c.Read(ctx)
			if err != nil {
				t.Fatal(err)
			}
			if typ == websocket.MessageText {
				var msg message
				json.Unmarshal(data, &msg)
				if msg.Type != "done" {
					t.Fatalf("čekal jsem done, přišlo %s", data)
				}
				break
			}
			got = append(got, data...)
		}
		if !bytes.Equal(got, audio) {
			t.Fatalf("kolo %d: vrácené audio se liší (%d B místo %d B)", round, len(got), len(audio))
		}
	}
}

func TestBadToken(t *testing.T) {
	ctx, c := dial(t)
	send(t, ctx, c, message{Type: "hello", Device: "test", Token: "spatne"})
	if msg := expect(t, ctx, c, "error"); msg.Code != "auth" {
		t.Fatalf("čekal jsem kód auth, přišlo %q", msg.Code)
	}
}

func TestWrongFormat(t *testing.T) {
	ctx, c := dial(t)
	send(t, ctx, c, message{Type: "hello", Device: "test", Token: "tajne"})
	send(t, ctx, c, message{Type: "start", Format: "pcm16", Rate: 44100})
	if msg := expect(t, ctx, c, "error"); msg.Code != "format" {
		t.Fatalf("čekal jsem kód format, přišlo %q", msg.Code)
	}
}

func TestWAVHeader(t *testing.T) {
	w := wav(make([]byte, 320))
	if len(w) != 364 || string(w[0:4]) != "RIFF" || string(w[36:40]) != "data" {
		t.Fatalf("špatná hlavička: % x", w[:44])
	}
}
