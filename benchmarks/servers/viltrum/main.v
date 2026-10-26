module main

import json
import os
import viltrum

struct Rating {
	score int
	count int
}

struct Item {
	id       int
	name     string
	category string
	price    int
	quantity int
	active   bool
	tags     []string
	rating   Rating
mut:
	total i64
}

struct Page {
	items []Item
	count int
}

fn main() {
	mut workers := 2
	mut base_port := 18085
	mut cert_file := 'benchmarks/certs/server.crt'
	mut key_file := 'benchmarks/certs/server.key'

	if os.args.len > 1 {
		workers = os.args[1].int()
	}
	if os.args.len > 2 {
		base_port = os.args[2].int()
	}
	if os.args.len > 3 {
		cert_file = os.args[3]
	}
	if os.args.len > 4 {
		key_file = os.args[4]
	}

	if !os.exists(cert_file) || !os.exists(key_file) {
		if os.exists('certs/server.crt') && os.exists('certs/server.key') {
			cert_file = 'certs/server.crt'
			key_file = 'certs/server.key'
		}
	}

	dataset_paths := [
		'benchmarks/data/dataset.json',
		'data/dataset.json',
		'../data/dataset.json',
		'/data/dataset.json',
	]

	mut dataset_raw := '[]'
	for p in dataset_paths {
		if os.exists(p) {
			dataset_raw = os.read_file(p) or { '[]' }
			break
		}
	}
	dataset := json.decode([]Item, dataset_raw) or { []Item{} }

	mut app := viltrum.new()
	app.server_options(viltrum.Options{
		workers: workers
	})

	// One copy of the dataset per worker: totals are written per request without locks
	mut copies := [][]Item{}
	num_workers := app.worker_count()
	for _ in 0 .. num_workers {
		copies << dataset.clone()
	}

	// 1. HttpArena Baseline (/baseline)
	app.get('/baseline', baseline)
	app.post('/baseline', baseline)

	// 2. HttpArena Pipeline (/pipeline)
	app.get('/pipeline', fn (mut c viltrum.Ctx) {
		c.text(200, 'ok')
	})

	// 3. HttpArena Dynamic JSON from Dataset with Totals
	app.get('/json/:count', fn [mut copies] (mut c viltrum.Ctx) {
		w := c.worker_id()
		mut n := int(c.param_int('count') or { 0 })
		if n < 0 {
			n = 0
		} else if w < copies.len && n > copies[w].len {
			n = copies[w].len
		}
		m := c.query_int('m') or { 1 }
		if w < copies.len {
			for i in 0 .. n {
				copies[w][i].total = i64(copies[w][i].price) * i64(copies[w][i].quantity) * m
			}
			c.json(200, Page{
				items: first(copies[w], n)
				count: n
			})
		} else {
			c.json(200, Page{
				items: []Item{}
				count: 0
			})
		}
	})

	// 4. HttpArena Non-blocking Async Timer Delay
	app.get('/delay/:ms', fn (mut c viltrum.Ctx) {
		ms := c.param_int('ms') or { -1 }
		if ms < 0 {
			c.text(400, 'bad delay')
			return
		}
		c.after(int(ms), delay_done, u64(ms))
	})

	// 5. Stream Echo (8gbit)
	app.post('/echo', fn (mut c viltrum.Ctx) {
		c.bytes(200, 'application/octet-stream', c.body())
	})

	// 6. WebSocket Echo
	app.ws('/ws', viltrum.WsHandler{
		on_message: fn (mut s viltrum.Ws, m viltrum.WsMessage) {
			if m.binary {
				s.send_binary(m.data)
			} else {
				s.send_text(m.text())
			}
		}
	})

	port_plain := base_port
	port_tls := base_port + 1
	port_h2 := base_port + 2

	mut listeners := [
		viltrum.Listener{
			addr: '0.0.0.0:${port_plain}'
		},
	]

	has_certs := os.exists(cert_file) && os.exists(key_file)
	if has_certs {
		listeners << viltrum.Listener{
			addr: '0.0.0.0:${port_tls}'
			tls: viltrum.TlsOptions{
				cert_file: cert_file
				key_file: key_file
			}
		}
		listeners << viltrum.Listener{
			addr: '0.0.0.0:${port_h2}'
			tls: viltrum.TlsOptions{
				cert_file: cert_file
				key_file: key_file
			}
			http2: true
		}
	}

	println('===================================================================')
	println(' [Viltrum Unified Server] Running with ${workers} worker threads (HttpArena Spec)')
	println('   -> Plain HTTP/1.1 & WS : http://0.0.0.0:${port_plain}')
	if has_certs {
		println('   -> TLS 1.3 HTTP/1.1    : https://0.0.0.0:${port_tls}')
		println('   -> TLS 1.3 HTTP/2      : https://0.0.0.0:${port_h2}')
	}
	println('===================================================================')

	app.serve(listeners) or { panic(err) }
}

fn first(a []Item, n int) []Item {
	mut v := unsafe { a }
	unsafe {
		v.len = n
	}
	return v
}

fn delay_done(mut c viltrum.Ctx, ms u64) {
	c.text_int(200, i64(ms))
}

fn baseline(mut c viltrum.Ctx) {
	sum := (c.query_int('a') or { 0 }) + (c.query_int('b') or { 0 }) + (c.body_int() or { 0 })
	c.text_int(200, sum)
}
