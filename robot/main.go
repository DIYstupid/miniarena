package main

import (
	"flag"
	"fmt"
	"os"
	"time"
)

func main() {
	serverAddr := flag.String("addr", "127.0.0.1:9000", "server address")
	botCount := flag.Int("bots", 100, "number of bots")
	connectRate := flag.Int("rate", 50, "connections per second")
	durationSec := flag.Int("duration", 30, "test duration in seconds")
	flag.Parse()

	if *botCount <= 0 {
		fmt.Fprintln(os.Stderr, "bots must be > 0")
		os.Exit(1)
	}

	ctrl := NewController(*serverAddr, *botCount, *connectRate, 5)
	ctrl.Run(time.Duration(*durationSec) * time.Second)
}
