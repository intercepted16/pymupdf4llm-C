package testutil

import (
	"os"
	"path/filepath"
)

var TestDataDir string

func init() {
	root := FindProjectRoot()
	if root != "" {
		TestDataDir = filepath.Join(root, "test_data", "pdfs")
	}
}

func FindProjectRoot() string {
	cwd, err := os.Getwd()
	if err != nil {
		return ""
	}
	for {
		if _, err := os.Stat(filepath.Join(cwd, ".root")); err == nil {
			return cwd
		}
		parent := filepath.Dir(cwd)
		if parent == cwd {
			return ""
		}
		cwd = parent
	}
}
