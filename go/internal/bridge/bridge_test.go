package bridge

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

var testPdfPath string

const deleteTempDir = true

func init() {
	testPdfPath = findProjectRoot()
	if testPdfPath != "" {
		testPdfPath = filepath.Join(testPdfPath, "test_data", "pdfs", "nist.pdf")
	}
}

func findProjectRoot() string {
	cwd, err := os.Getwd()
	if err != nil {
		return ""
	}
	for {
		rootMarker := filepath.Join(cwd, ".root")
		if _, err := os.Stat(rootMarker); err == nil {
			return cwd
		}
		parent := filepath.Dir(cwd)
		if parent == cwd {
			return ""
		}
		cwd = parent
	}
}

func TestExtractAndAnalyze(t *testing.T) {
	if testPdfPath == "" {
		t.Fatal("could not find project root (.root file)")
	}

	if _, err := os.Stat(testPdfPath); err != nil {
		t.Fatalf("test PDF not found at %s: %v", testPdfPath, err)
	}

	tempDir, err := ExtractAllPagesRaw(testPdfPath)
	if err != nil {
		t.Fatalf("extraction failed: %v", err)
	}
	Logger.Info("extraction temp dir", "path", tempDir)

	if deleteTempDir {
		defer os.RemoveAll(tempDir)
	}

	files, err := os.ReadDir(tempDir)
	if err != nil {
		t.Fatalf("failed to read temp dir: %v", err)
	}

	var pageFiles []string
	for _, f := range files {
		if strings.HasSuffix(f.Name(), ".raw") {
			pageFiles = append(pageFiles, f.Name())
		}
	}

	if len(pageFiles) == 0 {
		t.Fatal("no .raw files extracted")
	}
	Logger.Info("extracted pages", "count", len(pageFiles))

	var totalChars, totalWords, totalEdges, totalBoldChars, totalItalicChars int
	var medianPageData *RawPageData

	for i, pageFile := range pageFiles {
		data, err := ReadRawPage(filepath.Join(tempDir, pageFile))
		if err != nil {
			Logger.Info("warning: failed to read page", "file", pageFile, "error", err)
			continue
		}

		totalChars += len(data.Chars)
		totalEdges += len(data.Edges)

		for _, ch := range data.Chars {
			if ch.IsBold {
				totalBoldChars++
			}
			if ch.IsItalic {
				totalItalicChars++
			}
		}

		wordCount := 0
		inWord := false
		for _, ch := range data.Chars {
			if ch.Codepoint == ' ' || ch.Codepoint == '\n' || ch.Codepoint == '\t' {
				inWord = false
			} else if !inWord {
				wordCount++
				inWord = true
			}
		}
		totalWords += wordCount

		if i == len(pageFiles)/2 {
			medianPageData = data
		}
	}

	pdfStat, _ := os.Stat(testPdfPath)
	fileSizeMB := float64(pdfStat.Size()) / (1024 * 1024)

	var boldPercent, italicPercent float64
	if totalChars > 0 {
		boldPercent = (float64(totalBoldChars) / float64(totalChars)) * 100
		italicPercent = (float64(totalItalicChars) / float64(totalChars)) * 100
	}

	textSnippet := ""
	if medianPageData != nil && len(medianPageData.Chars) > 0 {
		snippetLen := 100
		if len(medianPageData.Chars) < snippetLen {
			snippetLen = len(medianPageData.Chars)
		}
		var sb strings.Builder
		for i := 0; i < snippetLen; i++ {
			sb.WriteRune(medianPageData.Chars[i].Codepoint)
		}
		textSnippet = strings.TrimSpace(sb.String())
		if len(textSnippet) > 80 {
			textSnippet = textSnippet[:80] + "..."
		}
	}

	Logger.Info("file", "name", filepath.Base(testPdfPath))
	Logger.Info("size", "mb", fileSizeMB)
	Logger.Info("pages", "count", len(pageFiles))
	Logger.Info("content stats")
	Logger.Info("total characters", "count", totalChars)
	Logger.Info("total words", "count", totalWords)
	Logger.Info("total edges", "count", totalEdges)
	Logger.Info("formatting stats")
	Logger.Info("bold characters", "percent", boldPercent)
	Logger.Info("italic characters", "percent", italicPercent)
	Logger.Info("median snippet", "text", textSnippet)

	if totalChars == 0 {
		t.Error("no characters extracted")
	}
	if totalWords == 0 {
		t.Error("no words extracted")
	}
}
