// Command mxl is the Go port of the Median XL Offline Tools CLI (src/main_cli.cpp): a command-line
// character editor for Diablo 2 - Median XL mod .d2s save files.
package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/character"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/resources"
)

func printUsage(w *os.File, appName string) {
	fmt.Fprintf(w, "Usage:\n")
	fmt.Fprintf(w, "  %s view <charfile.d2s>\n", appName)
	fmt.Fprintf(w, "  %s respec <stats|skills|both> <charfile.d2s> [--no-backup]\n", appName)
}

// dataPath resolves the resources directory: $DATA_PATH if set (mirrors the DATA_PATH compile definition
// used for debug builds in the original CMake setup), otherwise "<executable dir>/resources".
func dataPath() string {
	if p := os.Getenv("DATA_PATH"); p != "" {
		return p
	}
	exe, err := os.Executable()
	if err != nil {
		return "resources"
	}
	return filepath.Join(filepath.Dir(exe), "resources")
}

func locale() string {
	if l := os.Getenv("MXL_LANGUAGE"); l != "" {
		return l
	}
	return "en"
}

func newManager() *resources.Manager {
	return resources.NewManager(dataPath(), locale())
}

func runView(path string) int {
	c := character.New(newManager())
	if err := c.Load(path); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		return 1
	}
	fmt.Print(c.Summary())
	return 0
}

func runRespec(mode, path string, makeBackup bool) int {
	var targets character.RespecTarget
	switch mode {
	case "stats":
		targets = character.RespecStats
	case "skills":
		targets = character.RespecSkills
	case "both":
		targets = character.RespecStats | character.RespecSkills
	default:
		fmt.Fprintf(os.Stderr, "Error: unknown respec target '%s', expected stats|skills|both\n", mode)
		return 1
	}

	c := character.New(newManager())
	if err := c.Load(path); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		return 1
	}

	c.Respec(targets)

	backupPath, err := c.Save(path, makeBackup)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		return 1
	}

	if backupPath != "" {
		fmt.Printf("Backup created: %s\n", backupPath)
	}
	fmt.Printf("Respec (%s) applied to '%s'\n", mode, path)
	fmt.Print(c.Summary())
	return 0
}

func main() {
	os.Exit(run())
}

func run() int {
	appName := filepath.Base(os.Args[0])
	args := os.Args[1:]

	if len(args) == 0 {
		printUsage(os.Stderr, appName)
		return 1
	}

	command := args[0]
	args = args[1:]

	switch command {
	case "view":
		if len(args) != 1 {
			printUsage(os.Stderr, appName)
			return 1
		}
		return runView(args[0])

	case "respec":
		if len(args) < 2 || len(args) > 3 {
			printUsage(os.Stderr, appName)
			return 1
		}
		mode, path := args[0], args[1]
		makeBackup := true
		if len(args) == 3 {
			if args[2] != "--no-backup" {
				printUsage(os.Stderr, appName)
				return 1
			}
			makeBackup = false
		}
		return runRespec(mode, path, makeBackup)

	default:
		printUsage(os.Stderr, appName)
		return 1
	}
}
