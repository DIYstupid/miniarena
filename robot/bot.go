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

// Bot represents a single simulated player.
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

func NewBot(id int, stats *BotStats) *Bot {
	return &Bot{
		id:       id,
		username: fmt.Sprintf("bot_%d", id),
		stopCh:   make(chan struct{}),
		stats:    stats,
	}
}

// Run executes the full bot lifecycle: connect → login → match → battle → loop → end.
func (b *Bot) Run(serverAddr string) error {
	// 1. Connect
	conn, err := net.DialTimeout("tcp", serverAddr, 5*time.Second)
	if err != nil {
		return fmt.Errorf("bot %d: connect: %w", b.id, err)
	}
	b.conn = conn
	defer conn.Close()

	// 2. Login
	if err := b.doLogin(); err != nil {
		return fmt.Errorf("bot %d: login: %w", b.id, err)
	}

	// 3. Match
	if err := b.doMatch(); err != nil {
		return fmt.Errorf("bot %d: match: %w", b.id, err)
	}

	// 4. Enter room
	if err := b.doEnterRoom(); err != nil {
		return fmt.Errorf("bot %d: enter room: %w", b.id, err)
	}

	// 5. Ready
	if err := b.doReady(); err != nil {
		return fmt.Errorf("bot %d: ready: %w", b.id, err)
	}

	// 6. Wait for battle start
	if err := b.waitBattleStart(); err != nil {
		return fmt.Errorf("bot %d: wait battle: %w", b.id, err)
	}

	// 7. Battle loop
	b.battleLoop()

	return nil
}

func (b *Bot) doLogin() error {
	// LoginRequest: username (field 1) + password (field 2)
	payload := encodeLoginRequest(b.username, b.username) // password = username
	if err := SendFrame(b.conn, MsgLoginRequest, 0, b.nextSeq(), payload); err != nil {
		return err
	}

	f, err := DiscardUntil(&b.reader, b.conn, MsgLoginResponse)
	if err != nil {
		return err
	}
	if code := GetErrorCode(f.Payload); code != 0 {
		b.stats.LoginFail.Add(1)
		return fmt.Errorf("login failed: error_code=%d", code)
	}
	b.sessionID = GetSessionID(f.Payload)
	b.playerID = GetPlayerID(f.Payload)
	b.stats.LoginOK.Add(1)
	return nil
}

func (b *Bot) doMatch() error {
	// MatchStartRequest: mode (field 1) = 0
	payload := []byte{0x08, 0x00} // mode=0
	if err := SendFrame(b.conn, MsgMatchStartRequest, 0, b.nextSeq(), payload); err != nil {
		return err
	}

	// Wait for MatchStartResponse
	f, err := DiscardUntil(&b.reader, b.conn, MsgMatchStartResponse)
	if err != nil {
		return err
	}
	if code := GetErrorCode(f.Payload); code != 0 {
		b.stats.MatchFail.Add(1)
		return fmt.Errorf("match start failed: error_code=%d", code)
	}
	b.stats.MatchOK.Add(1)

	// Wait for MatchSuccessNotify (2004)
	f2, err := DiscardUntil(&b.reader, b.conn, MsgMatchSuccessNotify)
	if err != nil {
		return err
	}
	b.roomID = GetRoomID(f2.Payload)
	if b.roomID == 0 {
		return fmt.Errorf("match success: room_id=0")
	}
	return nil
}

func (b *Bot) doEnterRoom() error {
	// EnterRoomRequest: room_id (field 1) + session_id (field 2)
	payload := encodeEnterRoomRequest(b.roomID, b.sessionID)
	if err := SendFrame(b.conn, MsgEnterRoomRequest, 0, b.nextSeq(), payload); err != nil {
		return err
	}

	f, err := DiscardUntil(&b.reader, b.conn, MsgEnterRoomResponse)
	if err != nil {
		return err
	}
	if code := GetErrorCode(f.Payload); code != 0 {
		return fmt.Errorf("enter room failed: error_code=%d", code)
	}
	b.stats.RoomOK.Add(1)
	return nil
}

func (b *Bot) doReady() error {
	if err := SendFrame(b.conn, MsgPlayerReadyRequest, 0, b.nextSeq(), nil); err != nil {
		return err
	}
	return nil
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
	ticker := time.NewTicker(200 * time.Millisecond) // 5 ops/sec
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			b.sendRandomCommand()
		case <-b.stopCh:
			return
		default:
			// Also check for battle-end messages (non-blocking)
			f, err := b.reader.ReadFrame(b.conn)
			if err != nil {
				return
			}
			if f != nil {
				switch f.MsgID {
				case MsgBattleEndNotify:
					b.stats.EndReceived.Add(1)
					return
				case MsgPlayerDeathNotify:
					b.stats.DeathReceived.Add(1)
				}
			}
		}
	}
}

func (b *Bot) sendRandomCommand() {
	r := rand.Intn(100)
	switch {
	case r < 80:
		// Move
		dx := rand.Float32()*2 - 1
		dy := rand.Float32()*2 - 1
		payload := encodeMoveRequest(dx, dy)
		SendFrame(b.conn, MsgPlayerMoveRequest, 0, b.nextSeq(), payload)
		b.stats.MovesSent.Add(1)
	case r < 90:
		// Attack (target = random)
		payload := encodeAttackRequest(2) // simplified: target player 2
		SendFrame(b.conn, MsgPlayerAttackRequest, 0, b.nextSeq(), payload)
		b.stats.AttacksSent.Add(1)
	default:
		// Skill
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

func (b *Bot) Stop() {
	close(b.stopCh)
}

// --- Minimal protobuf payload constructors for request messages ---

func encodeLoginRequest(username, password string) []byte {
	// field 1: string username, field 2: string password
	// Tag 1 = 0x0a (string), Tag 2 = 0x12 (string)
	buf := make([]byte, 0, 128)
	buf = append(buf, 0x0a, byte(len(username)))
	buf = append(buf, []byte(username)...)
	buf = append(buf, 0x12, byte(len(password)))
	buf = append(buf, []byte(password)...)
	return buf
}

func encodeEnterRoomRequest(roomID, sessionID uint64) []byte {
	buf := make([]byte, 0, 32)
	// field 1: uint64 room_id
	buf = append(buf, 0x08, byte(roomID))
	// field 2: uint64 session_id
	buf = append(buf, 0x10, byte(sessionID))
	return buf
}

func encodeMoveRequest(dirX, dirY float32) []byte {
	// field 1: float direction_x, field 2: float direction_y
	buf := make([]byte, 10)
	buf[0] = 0x0d // tag 1, fixed32
	binary.LittleEndian.PutUint32(buf[1:5], math.Float32bits(dirX))
	buf[5] = 0x15 // tag 2, fixed32
	binary.LittleEndian.PutUint32(buf[6:10], math.Float32bits(dirY))
	return buf
}

func encodeAttackRequest(targetID uint64) []byte {
	return []byte{0x08, byte(targetID)}
}

func encodeSkillRequest(skillID int32, targetID uint64) []byte {
	return []byte{0x08, byte(skillID), 0x10, byte(targetID)}
}
