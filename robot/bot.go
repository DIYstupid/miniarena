package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"math/rand"
	"net"
	"sync/atomic"
	"time"
)

type Bot struct {
	id        int
	username  string
	conn      net.Conn
	reader    FrameReader
	sessionID uint64
	playerID  uint64
	roomID    uint64
	seq       uint64
	stopCh    chan struct{}
	stats     *BotStats
	latency   *LatencyStats
	cmdRate   int // commands per second
}

type BotStats struct {
	LoginOK       atomic.Int64
	LoginFail     atomic.Int64
	MatchOK       atomic.Int64
	MatchFail     atomic.Int64
	RoomOK        atomic.Int64
	BattleStart   atomic.Int64
	MovesSent     atomic.Int64
	AttacksSent   atomic.Int64
	SkillsSent    atomic.Int64
	DeathReceived atomic.Int64
	EndReceived   atomic.Int64
}

func NewBot(id int, stats *BotStats, latency *LatencyStats) *Bot {
	return &Bot{
		id:       id,
		username: fmt.Sprintf("bot_%d", id),
		stopCh:   make(chan struct{}),
		stats:    stats,
		latency:  latency,
		cmdRate:  5, // default
	}
}

func (b *Bot) Run(serverAddr string) error {
	start := time.Now()
	conn, err := net.DialTimeout("tcp", serverAddr, 5*time.Second)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	b.conn = conn
	defer conn.Close()

	b.recordLatency(start, "connect")

	if err := b.doLogin(); err != nil {
		return fmt.Errorf("login: %w", err)
	}

	// Login-only mode: stop here
	if b.cmdRate <= 0 {
		return nil
	}

	if err := b.doMatch(); err != nil {
		return fmt.Errorf("match: %w", err)
	}
	// doEnterRoom is not needed: MatchManager.tryMatch auto-adds players to room
	if err := b.doReady(); err != nil {
		return fmt.Errorf("ready: %w", err)
	}
	if err := b.waitBattleStart(); err != nil {
		return fmt.Errorf("battle: %w", err)
	}

	b.battleLoop()
	return nil
}

func (b *Bot) recordLatency(start time.Time, phase string) {
	if b.latency != nil {
		b.latency.Record(time.Since(start))
	}
}

func (b *Bot) doLogin() error {
	start := time.Now()
	payload := encodeLoginRequest(b.username, b.username)
	if err := SendFrame(b.conn, MsgLoginRequest, 0, b.nextSeq(), payload); err != nil {
		return err
	}
	f, err := DiscardUntil(&b.reader, b.conn, MsgLoginResponse)
	if err != nil {
		return err
	}
	if code := GetErrorCode(f.Payload); code != 0 {
		b.stats.LoginFail.Add(1)
		return fmt.Errorf("login error=%d", code)
	}
	b.sessionID = GetSessionID(f.Payload)
	b.playerID = GetPlayerID(f.Payload)
	b.stats.LoginOK.Add(1)
	b.recordLatency(start, "login")
	return nil
}

func (b *Bot) doMatch() error {
	payload := []byte{0x08, 0x00}
	if err := SendFrame(b.conn, MsgMatchStartRequest, 0, b.nextSeq(), payload); err != nil {
		return err
	}
	f, err := DiscardUntil(&b.reader, b.conn, MsgMatchStartResponse)
	if err != nil {
		return err
	}
	if code := GetErrorCode(f.Payload); code != 0 {
		b.stats.MatchFail.Add(1)
		return fmt.Errorf("match error=%d", code)
	}
	b.stats.MatchOK.Add(1)

	f2, err := DiscardUntil(&b.reader, b.conn, MsgMatchSuccessNotify)
	if err != nil {
		return err
	}
	b.roomID = GetRoomID(f2.Payload)
	// Players are auto-added to the room by MatchManager.tryMatch
	b.stats.RoomOK.Add(1)
	return nil
}

func (b *Bot) doEnterRoom() error {
	payload := encodeEnterRoomRequest(b.roomID, b.sessionID)
	if err := SendFrame(b.conn, MsgEnterRoomRequest, 0, b.nextSeq(), payload); err != nil {
		return err
	}
	f, err := DiscardUntil(&b.reader, b.conn, MsgEnterRoomResponse)
	if err != nil {
		return fmt.Errorf("enter room: %w", err)
	}
	code := GetErrorCode(f.Payload)
	if code != 0 {
		b.stats.MatchFail.Add(1)
		return fmt.Errorf("enter room error=%d", code)
	}
	b.stats.RoomOK.Add(1)
	return nil
}

func (b *Bot) doReady() error {
	return SendFrame(b.conn, MsgPlayerReadyRequest, 0, b.nextSeq(), nil)
}

func (b *Bot) waitBattleStart() error {
	_, err := DiscardUntil(&b.reader, b.conn, MsgBattleStartNotify)
	if err != nil {
		return err
	}
	b.stats.BattleStart.Add(1)
	return nil
}

func (b *Bot) battleLoop() {
	if b.cmdRate <= 0 {
		// Login-only mode: just wait for end or disconnect
		for {
			_, err := b.reader.ReadFrame(b.conn)
			if err != nil {
				return
			}
		}
	}

	interval := time.Second / time.Duration(b.cmdRate)
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			b.sendRandomCommand()
		}
	}
}

func (b *Bot) sendRandomCommand() {
	r := rand.Intn(100)
	switch {
	case r < 80:
		dx := rand.Float32()*2 - 1
		dy := rand.Float32()*2 - 1
		payload := encodeMoveRequest(dx, dy)
		SendFrame(b.conn, MsgPlayerMoveRequest, 0, b.nextSeq(), payload)
		b.stats.MovesSent.Add(1)
	case r < 90:
		payload := encodeAttackRequest(2)
		SendFrame(b.conn, MsgPlayerAttackRequest, 0, b.nextSeq(), payload)
		b.stats.AttacksSent.Add(1)
	default:
		skillID := int32(rand.Intn(3) + 1)
		payload := encodeSkillRequest(skillID, 2)
		SendFrame(b.conn, MsgPlayerSkillRequest, 0, b.nextSeq(), payload)
		b.stats.SkillsSent.Add(1)
	}
}

func (b *Bot) nextSeq() uint64 {
	b.seq++
	return b.seq
}

func encodeLoginRequest(username, password string) []byte {
	buf := make([]byte, 0, 128)
	buf = append(buf, 0x0a, byte(len(username)))
	buf = append(buf, []byte(username)...)
	buf = append(buf, 0x12, byte(len(password)))
	buf = append(buf, []byte(password)...)
	return buf
}

func encodeEnterRoomRequest(roomID, sessionID uint64) []byte {
	var buf []byte
	buf = append(buf, 0x08)
	buf = encodeVarint(buf, roomID)
	buf = append(buf, 0x10)
	buf = encodeVarint(buf, sessionID)
	return buf
}

func encodeMoveRequest(dirX, dirY float32) []byte {
	buf := make([]byte, 10)
	buf[0] = 0x0d
	binary.LittleEndian.PutUint32(buf[1:5], math.Float32bits(dirX))
	buf[5] = 0x15
	binary.LittleEndian.PutUint32(buf[6:10], math.Float32bits(dirY))
	return buf
}

func encodeAttackRequest(targetID uint64) []byte {
	var buf []byte
	buf = append(buf, 0x08)
	return encodeVarint(buf, targetID)
}

func encodeSkillRequest(skillID int32, targetID uint64) []byte {
	var buf []byte
	buf = append(buf, 0x08)
	buf = encodeVarint(buf, uint64(skillID))
	buf = append(buf, 0x10)
	return encodeVarint(buf, targetID)
}
