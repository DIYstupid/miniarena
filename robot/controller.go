package main

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
)

// Controller manages a fleet of bots.
type Controller struct {
	serverAddr  string
	botCount    int
	connectRate int // connections per second
	commandRate int // commands per second per bot

	stats    *BotStats
	global   GlobalStats
}

type GlobalStats struct {
	Connected    atomic.Int64
	Disconnected atomic.Int64
	Errors       atomic.Int64
	startTime    time.Time
}

func NewController(serverAddr string, botCount, connectRate, commandRate int) *Controller {
	return &Controller{
		serverAddr:  serverAddr,
		botCount:    botCount,
		connectRate: connectRate,
		commandRate: commandRate,
		stats:       &BotStats{},
	}
}

// Run launches all bots and waits for completion.
func (c *Controller) Run(duration time.Duration) {
	c.global.startTime = time.Now()

	var wg sync.WaitGroup
	delay := time.Second / time.Duration(c.connectRate)

	fmt.Printf("=== MiniArena Robot Controller ===\n")
	fmt.Printf("Server:    %s\n", c.serverAddr)
	fmt.Printf("Bots:      %d\n", c.botCount)
	fmt.Printf("Rate:      %d conn/s\n", c.connectRate)
	fmt.Printf("Duration:  %v\n\n", duration)

	for i := range c.botCount {
		wg.Add(1)
		bot := NewBot(i+1, c.stats)
		c.global.Connected.Add(1)

		go func() {
			defer wg.Done()
			defer c.global.Disconnected.Add(1)

			err := bot.Run(c.serverAddr)
			if err != nil {
				c.global.Errors.Add(1)
				fmt.Printf("bot %d: %v\n", bot.id, err)
			}
		}()

		time.Sleep(delay)
	}

	// Wait for duration or all bots to finish
	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()

	select {
	case <-done:
	case <-time.After(duration):
	}

	c.PrintReport()
}

func (c *Controller) PrintReport() {
	elapsed := time.Since(c.global.startTime)

	fmt.Printf("\n=== Test Report ===\n")
	fmt.Printf("Duration:       %v\n", elapsed.Round(time.Second))
	fmt.Printf("Connected:      %d\n", c.global.Connected.Load())
	fmt.Printf("Disconnected:   %d\n", c.global.Disconnected.Load())
	fmt.Printf("Errors:         %d\n", c.global.Errors.Load())
	fmt.Printf("\n--- Bot Stats ---\n")
	fmt.Printf("Login OK:       %d\n", c.stats.LoginOK.Load())
	fmt.Printf("Login Fail:     %d\n", c.stats.LoginFail.Load())
	fmt.Printf("Match OK:       %d\n", c.stats.MatchOK.Load())
	fmt.Printf("Match Fail:     %d\n", c.stats.MatchFail.Load())
	fmt.Printf("Room OK:        %d\n", c.stats.RoomOK.Load())
	fmt.Printf("Battle Start:   %d\n", c.stats.BattleStart.Load())
	fmt.Printf("Moves Sent:     %d\n", c.stats.MovesSent.Load())
	fmt.Printf("Attacks Sent:   %d\n", c.stats.AttacksSent.Load())
	fmt.Printf("Skills Sent:    %d\n", c.stats.SkillsSent.Load())
	fmt.Printf("Deaths:         %d\n", c.stats.DeathReceived.Load())
	fmt.Printf("End Received:   %d\n", c.stats.EndReceived.Load())
}
