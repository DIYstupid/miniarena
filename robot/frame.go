package main

import (
	"encoding/binary"
	"fmt"
	"io"
	"net"
)

const FrameHeaderSize = 18
const MaxPayload = 64 * 1024

const (
	MsgLoginRequest         = 1001
	MsgLoginResponse        = 1002
	MsgHeartbeatRequest     = 1101
	MsgHeartbeatResponse    = 1102
	MsgMatchStartRequest    = 2001
	MsgMatchStartResponse   = 2002
	MsgMatchSuccessNotify   = 2004
	MsgEnterRoomRequest     = 3001
	MsgEnterRoomResponse    = 3002
	MsgPlayerReadyRequest   = 3003
	MsgBattleStartNotify    = 3004
	MsgPlayerMoveRequest    = 4001
	MsgPlayerAttackRequest  = 4002
	MsgPlayerSkillRequest   = 4003
	MsgBattleStateNotify    = 4004
	MsgPlayerDeathNotify    = 4005
	MsgReconnectRequest     = 5001
	MsgReconnectResponse    = 5002
	MsgBattleSnapshotNotify = 5003
	MsgBattleEndNotify      = 6001
	MsgBattleSettlementNotify = 6002
)

func EncodeFrame(msgID uint32, flags uint16, seq uint64, payload []byte) []byte {
	if len(payload) > MaxPayload {
		panic("payload too large")
	}
	totalLen := uint32(FrameHeaderSize + len(payload))
	buf := make([]byte, totalLen)
	binary.BigEndian.PutUint32(buf[0:4], totalLen)
	binary.BigEndian.PutUint32(buf[4:8], msgID)
	binary.BigEndian.PutUint16(buf[8:10], flags)
	binary.BigEndian.PutUint64(buf[10:18], seq)
	copy(buf[18:], payload)
	return buf
}

type DecodedFrame struct {
	TotalLength uint32
	MsgID       uint32
	Flags       uint16
	Sequence    uint64
	Payload     []byte
}

type FrameReader struct {
	buf []byte
}

func (r *FrameReader) ReadFrame(conn net.Conn) (*DecodedFrame, error) {
	for len(r.buf) < FrameHeaderSize {
		chunk := make([]byte, 4096)
		n, err := conn.Read(chunk)
		if err != nil {
			return nil, err
		}
		r.buf = append(r.buf, chunk[:n]...)
	}

	totalLen := binary.BigEndian.Uint32(r.buf[0:4])
	msgID := binary.BigEndian.Uint32(r.buf[4:8])
	flags := binary.BigEndian.Uint16(r.buf[8:10])
	seq := binary.BigEndian.Uint64(r.buf[10:18])

	if totalLen < FrameHeaderSize || totalLen > FrameHeaderSize+MaxPayload {
		return nil, fmt.Errorf("invalid total_length: %d", totalLen)
	}

	for uint32(len(r.buf)) < totalLen {
		chunk := make([]byte, 4096)
		n, err := conn.Read(chunk)
		if err != nil {
			return nil, err
		}
		r.buf = append(r.buf, chunk[:n]...)
	}

	payload := make([]byte, totalLen-FrameHeaderSize)
	copy(payload, r.buf[FrameHeaderSize:totalLen])
	r.buf = r.buf[totalLen:]

	return &DecodedFrame{
		TotalLength: totalLen,
		MsgID:       msgID,
		Flags:       flags,
		Sequence:    seq,
		Payload:     payload,
	}, nil
}

func SendFrame(conn net.Conn, msgID uint32, flags uint16, seq uint64, payload []byte) error {
	data := EncodeFrame(msgID, flags, seq, payload)
	_, err := conn.Write(data)
	return err
}

func DiscardUntil(reader *FrameReader, conn net.Conn, msgID uint32) (*DecodedFrame, error) {
	for {
		f, err := reader.ReadFrame(conn)
		if err != nil {
			return nil, err
		}
		if f.MsgID == msgID {
			return f, nil
		}
	}
}

// parseVarint reads a varint from protobuf payload starting at pos.
// Returns (value, newPos).
func parseVarint(payload []byte, pos int) (uint64, int) {
	var v uint64
	var shift uint
	for pos < len(payload) {
		b := payload[pos]
		pos++
		v |= uint64(b&0x7F) << shift
		if b < 0x80 {
			break
		}
		shift += 7
	}
	return v, pos
}

// GetErrorCode extracts error_code from a protobuf payload.
// Proto3 omits default values (0), so absence means success.
func GetErrorCode(payload []byte) int32 {
	pos := 0
	for pos < len(payload) {
		tag, newPos := parseVarint(payload, pos)
		pos = newPos
		fieldNum := int(tag >> 3)
		wireType := int(tag & 0x7)
		switch wireType {
		case 0: // varint
			val, next := parseVarint(payload, pos)
			pos = next
			if fieldNum == 1 {
				return int32(val)
			}
		case 2: // length-delimited
			length, next := parseVarint(payload, pos)
			pos = next + int(length)
		default:
			return 0
		}
	}
	return 0 // not present → default = success
}

// GetSessionID extracts session_id (uint64 field 2) from LoginResponse.
func GetSessionID(payload []byte) uint64 {
	pos := 0
	for pos < len(payload) {
		tag, newPos := parseVarint(payload, pos)
		pos = newPos
		fieldNum := int(tag >> 3)
		wireType := int(tag & 0x7)
		switch wireType {
		case 0:
			val, next := parseVarint(payload, pos)
			pos = next
			if fieldNum == 2 {
				return val
			}
		case 2:
			length, next := parseVarint(payload, pos)
			pos = next + int(length)
		default:
			return 0
		}
	}
	return 0
}

// GetPlayerID extracts player_id (uint64 field 3) from LoginResponse.
func GetPlayerID(payload []byte) uint64 {
	pos := 0
	for pos < len(payload) {
		tag, newPos := parseVarint(payload, pos)
		pos = newPos
		fieldNum := int(tag >> 3)
		wireType := int(tag & 0x7)
		switch wireType {
		case 0:
			val, next := parseVarint(payload, pos)
			pos = next
			if fieldNum == 3 {
				return val
			}
		case 2:
			length, next := parseVarint(payload, pos)
			pos = next + int(length)
		default:
			return 0
		}
	}
	return 0
}

// GetRoomID extracts room_id (uint64 field 1) from MatchSuccessNotify.
func GetRoomID(payload []byte) uint64 {
	pos := 0
	for pos < len(payload) {
		tag, newPos := parseVarint(payload, pos)
		pos = newPos
		fieldNum := int(tag >> 3)
		wireType := int(tag & 0x7)
		switch wireType {
		case 0:
			val, next := parseVarint(payload, pos)
			pos = next
			if fieldNum == 1 {
				return val
			}
		case 2:
			length, next := parseVarint(payload, pos)
			pos = next + int(length)
		default:
			return 0
		}
	}
	return 0
}

func ReadFull(r io.Reader, buf []byte) error {
	_, err := io.ReadFull(r, buf)
	return err
}
