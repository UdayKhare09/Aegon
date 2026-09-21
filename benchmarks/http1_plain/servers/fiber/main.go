package main

import (
	"fmt"
	"os"
	"strconv"
	"time"

	"github.com/gofiber/fiber/v2"
	"github.com/google/uuid"
)

type JsonMessage struct {
	Message   string `json:"message"`
	ID        string `json:"id"`
	Timestamp int64  `json:"timestamp"`
}

type PostInfo struct {
	UserID uint64 `json:"user_id"`
	PostID uint64 `json:"post_id"`
}

func main() {
	port := 18082
	if len(os.Args) > 2 {
		if p, err := strconv.Atoi(os.Args[2]); err == nil {
			port = p
		}
	}

	app := fiber.New(fiber.Config{
		Prefork:               true,
		DisableStartupMessage: true,
	})

	// 1. Plaintext
	app.Get("/plaintext", func(c *fiber.Ctx) error {
		c.Set("Content-Type", "text/plain; charset=utf-8")
		return c.SendString("Hello, World!")
	})

	// 2. JSON
	app.Get("/json", func(c *fiber.Ctx) error {
		msg := JsonMessage{
			Message:   "Hello, World!",
			ID:        uuid.New().String(),
			Timestamp: time.Now().UnixMicro(),
		}
		c.Set("Content-Type", "application/json; charset=utf-8")
		return c.JSON(msg)
	})

	// 3. Dynamic Route
	app.Get("/users/:id/posts/:post_id", func(c *fiber.Ctx) error {
		userID, _ := strconv.ParseUint(c.Params("id"), 10, 64)
		postID, _ := strconv.ParseUint(c.Params("post_id"), 10, 64)
		c.Set("Content-Type", "application/json; charset=utf-8")
		return c.JSON(PostInfo{
			UserID: userID,
			PostID: postID,
		})
	})

	addr := fmt.Sprintf("0.0.0.0:%d", port)
	if !fiber.IsChild() {
		fmt.Printf("Fiber listening on http://%s (prefork enabled)\n", addr)
	}
	if err := app.Listen(addr); err != nil {
		fmt.Println("Error:", err)
	}
}
