package main

/*
#include <stdlib.h>
*/
import "C"
import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"unsafe"
	"fmt"
	"time"

	"github.com/pymupdf4llm-c/go/internal/bridge"
	"github.com/pymupdf4llm-c/go/internal/extractor"
	"github.com/pymupdf4llm-c/go/internal/logger"

)

var (
	debugLog = os.Getenv("TOMD_DEBUG") != ""
	Logger   = logger.GetLogger("tomd")
)

//export pdf_to_json
func pdf_to_json(pdf_path *C.char, output_file *C.char) C.int {
	pdfPath, outputFile := C.GoString(pdf_path), C.GoString(output_file)
	err := pdfToJson(pdfPath, outputFile)
	if err == nil {
		return 0
	}
	return -1;
}

func pdfToJson(pdfPath, outputPath string) error {
	startTotal := time.Now()      // total runtime timer
	startRaw := time.Now()        // raw data timer

	Logger.Info("beginning conversion...")
	Logger.Debug("paths: pdf=%s output=%s", pdfPath, outputPath)

	
	tempRawDir, err := bridge.ExtractAllPagesRaw(pdfPath)
	rawElapsed := time.Since(startRaw) // record raw extraction time
	if err != nil {
		Logger.Error("extraction error: %v", err)
		return err
	}
	defer os.RemoveAll(tempRawDir)

	
	entries, err := os.ReadDir(tempRawDir)
	if err != nil {
		Logger.Error("readdir error: %v", err)
		return err
	}
	var pageFiles []string
	for _, e := range entries {
		if strings.HasPrefix(e.Name(), "page_") && strings.HasSuffix(e.Name(), ".raw") {
			pageFiles = append(pageFiles, filepath.Join(tempRawDir, e.Name()))
		}
	}
	sort.Slice(pageFiles, func(i, j int) bool { return extractPageNum(pageFiles[i]) < extractPageNum(pageFiles[j]) })

	type pageResult struct {
		pageNum int
		json    []byte
		err     error
	}
	results := make([]pageResult, len(pageFiles))
	numWorkers := runtime.NumCPU()
	var wg sync.WaitGroup
	pageChan := make(chan int, numWorkers)

	for i := 0; i < numWorkers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for idx := range pageChan {
				rawData, err := bridge.ReadRawPage(pageFiles[idx])
				if err != nil {
					results[idx] = pageResult{err: err}
					continue
				}
				page := extractor.ExtractPageFromRaw(rawData)
				pageJSON, err := json.Marshal(page)
				if err != nil {
					results[idx] = pageResult{err: err}
					continue
				}
				results[idx] = pageResult{pageNum: page.Number, json: pageJSON}
				Logger.Debug("processed page %d", page.Number)
			}
		}()
	}

	for i := range pageFiles {
		pageChan <- i
	}
	close(pageChan)
	wg.Wait()

	for _, res := range results {
		if res.err != nil {
			Logger.Error("processing error: %v", res.err)
			return res.err
		}
	}

	
	outFile, err := os.Create(outputPath)
	if err != nil {
		Logger.Error("output file error: %v", err)
		return err
	}
	defer outFile.Close()

	writer := bufio.NewWriterSize(outFile, 256*1024)
	defer writer.Flush()

	if _, err := writer.WriteString("["); err != nil {
		Logger.Error("write error: %v", err)
		return err
	}
	for i, res := range results {
		if i > 0 {
			if _, err := writer.WriteString(","); err != nil {
				Logger.Error("write error: %v", err)
				return err
			}
		}
		if _, err := writer.Write(res.json); err != nil {
			Logger.Error("write error: %v", err)
			return err
		}
		Logger.Debug("wrote page %d", res.pageNum)
	}
	if _, err := writer.WriteString("]"); err != nil {
		Logger.Error("write error: %v", err)
		return err
	}

	
	totalElapsed := time.Since(startTotal)
	Logger.Info("raw data extraction", "timeInC", rawElapsed)
	Logger.Info("high level data extraction", "timeInGo", (totalElapsed - rawElapsed))
	Logger.Info("total conversion time", "totalTime", totalElapsed)

	Logger.Info("success")
	return nil
}
//export free_string
func free_string(s *C.char) { C.free(unsafe.Pointer(s)) }

func extractPageNum(filename string) int {
	base := filepath.Base(filename)
	base = strings.TrimPrefix(base, "page_")
	base = strings.TrimSuffix(base, ".raw")
	base = strings.TrimSuffix(base, ".json")
	num, _ := strconv.Atoi(base)
	return num
}

func init() {
	if debugLog {
		Logger.Debug("[tomd] library loaded")
	}
}

func main() {
	if (len(os.Args) < 3) {
		fmt.Println("Usage: ./program <input.pdf> [output_json]")
	}
	pdfToJson(os.Args[1], os.Args[2]);
}
