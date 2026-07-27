package main

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
)

type Controller struct {
	serverAddr  string
	botCount    int
	connectRate int
	commandRate int

	stats    BotStats
	global   GlobalStats
	latencies LatencyStats
}

type GlobalStats struct {
	Connected    atomic.Int64
	Disconnected atomic.Int64
	Errors       atomic.Int64
	startTime    time.Time
}

type LatencyStats struct {
	mu    sync.Mutex
	diffs []time.Duration
}

func (l *LatencyStats) Record(d time.Duration) {
	l.mu.Lock()
	l.diffs = append(l.diffs, d)
	l.mu.Unlock()
}

func (l *LatencyStats) Percentile(p float64) time.Duration {
	l.mu.Lock()
	defer l.mu.Unlock()
	if len(l.diffs) == 0 {
		return 0
	}
	idx := int(float64(len(l.diffs)) * p)
	if idx >= len(l.diffs) {
		idx = len(l.diffs) - 1
	}
	return l.diffs[idx]
}

func (l *LatencyStats) Count() int {
	l.mu.Lock()
	defer l.mu.Unlock()
	return len(l.diffs)
}

func NewController(serverAddr string, botCount, connectRate, commandRate int) *Controller {
	return &Controller{
		serverAddr:  serverAddr,
		botCount:    botCount,
		connectRate: connectRate,
		commandRate: commandRate,
		stats:       BotStats{},
	}
}

func (c *Controller) Run(duration time.Duration) {
	c.global.startTime = time.Now()

	var wg sync.WaitGroup
	delay := time.Second / time.Duration(c.connectRate)

	fmt.Printf("=== MiniArena Robot Controller ===\n")
	fmt.Printf("Server:  %s\n", c.serverAddr)
	fmt.Printf("Bots:    %d\n", c.botCount)
	fmt.Printf("Rate:    %d conn/s, %d cmd/s/bot\n", c.connectRate, c.commandRate)
	fmt.Printf("Time:    %v\n\n", duration)

	var liveCount atomic.Int64

	for i := range c.botCount {
		wg.Add(1)
		bot := NewBot(i+1, &c.stats, &c.latencies)
		bot.cmdRate = c.commandRate
		c.global.Connected.Add(1)
		liveCount.Add(1)

		go func() {
			defer wg.Done()
			defer c.global.Disconnected.Add(1)
			defer liveCount.Add(-1)

			if err := bot.Run(c.serverAddr); err != nil {
				c.global.Errors.Add(1)
			}
		}()

		time.Sleep(delay)
	}

	// Monitor progress
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()
	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()

	for {
		select {
		case <-ticker.C:
			elapsed := time.Since(c.global.startTime)
			fmt.Printf("[%v] live=%d login=%d match=%d battle=%d moves=%d\n",
				elapsed.Round(time.Second), liveCount.Load(),
				c.stats.LoginOK.Load(), c.stats.MatchOK.Load(),
				c.stats.BattleStart.Load(), c.stats.MovesSent.Load())
		case <-done:
			c.PrintReport()
			return
		case <-time.After(duration):
			c.PrintReport()
			return
		}
	}
}

func (c *Controller) PrintReport() {
	elapsed := time.Since(c.global.startTime)

	fmt.Printf("\n=== Pressure Test Report ===\n")
	fmt.Printf("Duration:     %v\n", elapsed.Round(time.Second))
	fmt.Printf("Connected:    %d\n", c.global.Connected.Load())
	fmt.Printf("Disconnected: %d\n", c.global.Disconnected.Load())
	fmt.Printf("Errors:       %d\n", c.global.Errors.Load())
	fmt.Printf("\n--- Flow ---\n")
	fmt.Printf("Login OK:     %d\n", c.stats.LoginOK.Load())
	fmt.Printf("Login Fail:   %d\n", c.stats.LoginFail.Load())
	fmt.Printf("Match OK:     %d\n", c.stats.MatchOK.Load())
	fmt.Printf("Match Fail:   %d\n", c.stats.MatchFail.Load())
	fmt.Printf("Room OK:      %d\n", c.stats.RoomOK.Load())
	fmt.Printf("Battle Start: %d\n", c.stats.BattleStart.Load())
	fmt.Printf("\n--- Combat ---\n")
	fmt.Printf("Moves Sent:   %d\n", c.stats.MovesSent.Load())
	fmt.Printf("Attacks Sent: %d\n", c.stats.AttacksSent.Load())
	fmt.Printf("Skills Sent:  %d\n", c.stats.SkillsSent.Load())
	fmt.Printf("Deaths:       %d\n", c.stats.DeathReceived.Load())
	fmt.Printf("End Received: %d\n", c.stats.EndReceived.Load())
	fmt.Printf("\n--- Latency ---\n")
	fmt.Printf("Samples:      %d\n", c.latencies.Count())
	if c.latencies.Count() > 0 {
		fmt.Printf("P50:          %v\n", c.latencies.Percentile(0.50))
		fmt.Printf("P95:          %v\n", c.latencies.Percentile(0.95))
		fmt.Printf("P99:          %v\n", c.latencies.Percentile(0.99))
	}
	fmt.Printf("\n--- Rates ---\n")
	sec := elapsed.Seconds()
	if sec > 0 {
		fmt.Printf("Login/s:      %.1f\n", float64(c.stats.LoginOK.Load())/sec)
		fmt.Printf("Match/s:      %.1f\n", float64(c.stats.MatchOK.Load())/sec)
		fmt.Printf("Moves/s:      %.1f\n", float64(c.stats.MovesSent.Load())/sec)
	}
}
