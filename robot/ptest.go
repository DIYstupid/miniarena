package main

import (
	"flag"
	"fmt"
	"os"
	"time"
)

func main() {
	serverAddr := flag.String("addr", "127.0.0.1:9000", "server address")
	testName := flag.String("test", "", "test name: conn, battle, throughput, stability")
	botCount := flag.Int("bots", 100, "number of bots (manual mode)")
	connectRate := flag.Int("rate", 50, "connections per second (manual mode)")
	durationSec := flag.Int("duration", 30, "test duration in seconds (manual mode)")
	flag.Parse()

	// Manual mode: if -test is empty, use direct params
	if *testName == "" {
		ctrl := NewController(*serverAddr, *botCount, *connectRate, 5)
		ctrl.Run(time.Duration(*durationSec) * time.Second)
		return
	}

	switch *testName {
	case "conn":
		runConnTest(*serverAddr)
	case "battle":
		runBattleTest(*serverAddr)
	case "throughput":
		runThroughputTest(*serverAddr)
	case "stability":
		runStabilityTest(*serverAddr)
	default:
		fmt.Fprintf(os.Stderr, "unknown test: %s\n", *testName)
		os.Exit(1)
	}
}

func runConnTest(addr string) {
	stages := []struct {
		count int
		rate  int
		label string
	}{
		{500, 100, "500 conn / 100/s"},
		{1000, 200, "1000 conn / 200/s"},
		{2000, 300, "2000 conn / 300/s"},
	}

	for _, s := range stages {
		fmt.Printf("\n--- %s ---\n", s.label)
		ctrl := NewController(addr, s.count, s.rate, 0)
		ctrl.Run(5 * time.Second)
	}
}

func runBattleTest(addr string) {
	fmt.Println("\n=== Battle Test: 100 bots, 10/room ===")
	ctrl := NewController(addr, 100, 50, 5)
	ctrl.Run(30 * time.Second)
}

func runThroughputTest(addr string) {
	fmt.Println("\n=== Throughput Test: 50 bots, 10 ops/s ===")
	ctrl := NewController(addr, 50, 25, 10)
	ctrl.Run(20 * time.Second)
}

func runStabilityTest(addr string) {
	fmt.Println("\n=== Stability Test: 100 bots, 2 min ===")
	ctrl := NewController(addr, 100, 30, 3)
	ctrl.Run(120 * time.Second)
}
