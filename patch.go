package main

/*
#cgo CFLAGS: -g -Wall
#cgo LDFLAGS: -lm
#include "patch.c"

*/
import "C"
import (
	"errors"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"unsafe"
)

// isValidMacho 判断是否是有效macho文件
func isValidMacho(filePath string) bool {
	if filePath == "" {
		return false
	}
	h, err := os.Open(filePath)
	if err != nil {
		return false
	}
	defer h.Close()

	buf := make([]byte, 4)
	_, err = io.ReadFull(h, buf)
	if err != nil {
		return false
	}
	if (buf[0] == 202 && buf[1] == 254 && buf[2] == 186 && buf[3] == 190) || (buf[0] == 207 && buf[1] == 250 && buf[2] == 237 && buf[3] == 254) {
		return true
	}
	return false
}

// patchMacho 修补macho文件
func patchMacho(filePath string) error {
	h, err := os.OpenFile(filePath, os.O_RDWR|os.O_CREATE, 0666)
	if err != nil {
		return err
	}
	defer h.Close()
	buf := make([]byte, 65535)
	_, err = h.Read(buf)
	// 应该不太可能出现小于64kb的文件, 所以不处理io.EOL
	if err != nil {
		return err
	}
	bufC := (*C.char)(unsafe.Pointer(&buf[0]))

	var errMsg [1024]byte
	errMsgC := (*C.char)(unsafe.Pointer(&errMsg[0]))
	ret := int(C.patch_for_simulator(bufC, errMsgC))
	if ret == 0 {
		return errors.New(C.GoString(errMsgC))
	}

	_, err = h.Seek(0, 0)
	if err != nil {
		return err
	}
	_, err = h.Write(buf)
	if err != nil {
		return err
	}
	return signMacho(filePath)
}

// signMacho 重签名macho文件
func signMacho(filePath string) error {
	// 要执行的Shell命令
	cmd := "/usr/bin/codesign -f -s - " + filePath
	command := exec.Command("sh", "-c", cmd)
	_, err := command.CombinedOutput()
	if err != nil {
		return err
	}
	return nil
}

func main() {
	var machoFiles []string
	var err error
	if len(os.Args) <= 1 {
		log.Printf("Usage: %s /IPA_Payload_Path\n", os.Args[0])
		os.Exit(1)
	}
	ipaPath := os.Args[1]

	log.Printf("正在获取macho文件目录...\n")
	err = filepath.Walk(ipaPath, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		if !info.IsDir() {
			if isValidMacho(path) {
				machoFiles = append(machoFiles, path)
			}
		}
		return nil
	})
	if err != nil {
		log.Fatalf("获取macho文件目录失败: %s\n", err)
	}

	for _, v := range machoFiles {
		log.Printf("正在修补: %s\n", v)
		if err = patchMacho(v); err != nil {
			log.Fatalf("修补 %s 失败: %s\n", v, err)
		} else {
			log.Printf("修补 %s 成功\n", v)
		}
	}
	log.Printf("全部修补完成")
	os.Exit(0)
}
