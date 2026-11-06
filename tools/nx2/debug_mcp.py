#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""MCP server (stdio) for Lichii's in-process debug server (docs/nx2/debug-mcp.md).

Run Lichii with EDEN_NX2_DEBUG_SERVER=127.0.0.1:6545, then register this script as a stdio MCP
server for the agent:  python3 -I tools/nx2/debug_mcp.py [--server 127.0.0.1:6545]
or run one request from a shell:
  python3 -I tools/nx2/debug_mcp.py --call '{"method":"screenshot","params":{}}' --out shot.png

Standard library only. Each tool call is forwarded as one JSON line to the emulator, which may
be restarted between calls: the adapter reconnects on demand.
"""

import argparse
import base64
import json
import os
import socket
import subprocess
import sys

PROTOCOL_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")
DEFAULT_SERVER = "127.0.0.1:6545"
HERE = os.path.dirname(os.path.abspath(__file__))

TOOLS = [
    {
        "name": "status",
        "description": "Game title, program id, build, renderer, paused state, presented frame "
                       "count and fps (measured over fps_window_ms), dump directory, live log "
                       "filter and the shader hashes rejected at runtime.",
        "inputSchema": {
            "type": "object",
            "properties": {"fps_window_ms": {"type": "integer", "minimum": 0, "maximum": 5000}},
        },
    },
    {
        "name": "screenshot",
        "description": "PNG of the next presented frame (16:9, default 720 lines). Fails when no "
                       "frame is presented within timeout_s (paused or loading).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "height": {"type": "integer", "minimum": 144, "maximum": 2160},
                "save": {"type": "boolean", "description": "Also write it into the dump directory."},
                "timeout_s": {"type": "integer", "minimum": 1, "maximum": 30},
            },
        },
    },
    {
        "name": "log_tail",
        "description": "Log lines kept in memory since the game booted. Without 'after', the newest "
                       "'limit' lines; with 'after' (the 'next' of an earlier call), the lines "
                       "logged since, oldest first. 'contains' keeps lines with that substring.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "after": {"type": "integer", "minimum": 0},
                "limit": {"type": "integer", "minimum": 1, "maximum": 5000},
                "contains": {"type": "string"},
            },
        },
    },
    {
        "name": "log_filter",
        "description": "Replaces the live log filter, e.g. '*:Info HW.GPU:Debug Shader:Debug'. "
                       "Returns the previous one. Lines filtered out are never logged.",
        "inputSchema": {
            "type": "object",
            "properties": {"filter": {"type": "string"}},
            "required": ["filter"],
        },
    },
    {
        "name": "capture_frame",
        "description": "EDEN_NX2_FRAME_DUMP for the next frame, now: every pass colour target, "
                       "dispatch storage image and the present source as PNG and .raw.zst, plus "
                       "manifest.jsonl. 'sampled' also reads back every sampled texture. Returns "
                       "the directory, summary, entry counts and the first manifest entries. "
                       "'index' builds index.html with frame_dump_index.py.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "sampled": {"type": "boolean"},
                "timeout_s": {"type": "integer", "minimum": 1, "maximum": 600},
                "manifest_lines": {"type": "integer", "minimum": 0, "maximum": 5000},
                "index": {"type": "boolean"},
            },
        },
    },
    {
        "name": "capture_image",
        "description": "One PNG of a capture made in this session, by entry name (the manifest "
                       "'name', without extension), e.g. '0042_pass007_d120-180_rt0'.",
        "inputSchema": {
            "type": "object",
            "properties": {"directory": {"type": "string"}, "name": {"type": "string"}},
            "required": ["directory", "name"],
        },
    },
    {
        "name": "reject_shaders",
        "description": "EDEN_NX2_REJECT_HASH at runtime: rejects (or with reject=false restores) "
                       "native programs by native program key (shader dump names) or shader hash "
                       "(frame dump 'shaders'); their cached pipelines are dropped at the next "
                       "frame end. With no hashes, lists the rejected ones.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "hashes": {"type": "array", "items": {"type": "string"}},
                "reject": {"type": "boolean"},
                "timeout_s": {"type": "integer", "minimum": 0, "maximum": 60},
            },
        },
    },
]

TOOL_NAMES = {tool["name"] for tool in TOOLS}


class EmulatorError(Exception):
    pass


class Emulator:
    """One JSON line per request to the debug server, reconnecting when the game restarted."""

    def __init__(self, address):
        host, _, port = address.rpartition(":")
        self.host = host.strip("[]") or "127.0.0.1"
        self.port = int(port)
        self.sock = None
        self.buffer = b""
        self.next_id = 1

    def close(self):
        if self.sock is not None:
            self.sock.close()
        self.sock = None
        self.buffer = b""

    def call(self, method, params, timeout):
        for attempt in (0, 1):
            try:
                if self.sock is None:
                    self.sock = socket.create_connection((self.host, self.port), timeout=5)
                self.sock.settimeout(timeout)
                request_id = self.next_id
                self.next_id += 1
                line = json.dumps({"id": request_id, "method": method, "params": params})
                self.sock.sendall(line.encode() + b"\n")
                while b"\n" not in self.buffer:
                    chunk = self.sock.recv(1 << 20)
                    if not chunk:
                        raise ConnectionResetError("the emulator closed the connection")
                    self.buffer += chunk
                raw, _, self.buffer = self.buffer.partition(b"\n")
                response = json.loads(raw)
                break
            except (ConnectionRefusedError, socket.timeout) as error:
                self.close()
                if isinstance(error, socket.timeout):
                    raise EmulatorError(f"no answer within {timeout} s") from error
                raise EmulatorError(
                    f"nothing listens on {self.host}:{self.port}: start Lichii with "
                    f"EDEN_NX2_DEBUG_SERVER={self.host}:{self.port} and boot a game") from error
            except OSError as error:
                # A stale connection from an earlier boot: reconnect once.
                self.close()
                if attempt == 1:
                    raise EmulatorError(str(error)) from error
        if "error" in response:
            raise EmulatorError(response["error"])
        return response.get("result")


def text(value):
    return {"type": "text", "text": value if isinstance(value, str) else json.dumps(value, indent=1)}


class Adapter:
    def __init__(self, emulator):
        self.emulator = emulator
        self.captures = set()

    def run(self, name, arguments):
        """Runs one tool; returns its result and the PNG it produced, if any."""
        if name not in TOOL_NAMES:
            raise EmulatorError(f"unknown tool {name}")
        if name == "capture_image":
            return self.capture_image(arguments)
        params = dict(arguments)
        index = params.pop("index", False)
        timeout = max(int(params.get("timeout_s", 120 if name == "capture_frame" else 5)), 1) + 30
        result = self.emulator.call(name, params, timeout)
        if name == "screenshot":
            png = result.pop("png_base64", None)
            return result, base64.b64decode(png) if png else None
        if name == "capture_frame":
            self.captures.add(os.path.realpath(result["directory"]))
            if index:
                result["index"] = self.build_index(result["directory"])
        return result, None

    def call_tool(self, name, arguments):
        result, png = self.run(name, arguments)
        content = [text(result)]
        if png is not None:
            content.insert(0, {"type": "image", "data": base64.b64encode(png).decode(),
                               "mimeType": "image/png"})
        return content

    def build_index(self, directory):
        script = os.path.join(HERE, "frame_dump_index.py")
        done = subprocess.run([sys.executable, "-I", script, directory], capture_output=True,
                              text=True, timeout=600)
        if done.returncode != 0:
            return f"frame_dump_index.py failed: {done.stderr.strip()[-2000:]}"
        return os.path.join(directory, "index.html")

    def capture_image(self, arguments):
        directory = os.path.realpath(arguments["directory"])
        if directory not in self.captures:
            raise EmulatorError("not a capture made in this session; run capture_frame first")
        path = os.path.realpath(os.path.join(directory, arguments["name"] + ".png"))
        if os.path.dirname(path) != directory or not os.path.isfile(path):
            raise EmulatorError(f"no image {arguments['name']}.png in {directory}")
        with open(path, "rb") as file:
            return {"path": path}, file.read()

    def handle(self, message):
        """Returns the response to one JSON-RPC message, or None for a notification."""
        method = message.get("method")
        if "id" not in message:
            return None
        reply = {"jsonrpc": "2.0", "id": message["id"]}
        params = message.get("params") or {}
        if method == "initialize":
            wanted = params.get("protocolVersion")
            reply["result"] = {
                "protocolVersion": wanted if wanted in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0],
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "lichii-debug", "version": "1"},
            }
        elif method == "ping":
            reply["result"] = {}
        elif method == "tools/list":
            reply["result"] = {"tools": TOOLS}
        elif method == "tools/call":
            try:
                content = self.call_tool(params.get("name"), params.get("arguments") or {})
                reply["result"] = {"content": content, "isError": False}
            except (EmulatorError, KeyError, ValueError) as error:
                message_text = f"missing argument {error}" if isinstance(error, KeyError) else str(error)
                reply["result"] = {"content": [text(message_text)], "isError": True}
        else:
            reply["error"] = {"code": -32601, "message": f"method not found: {method}"}
        return reply


def serve(adapter, stdin, stdout):
    for line in stdin:
        if not line.strip():
            continue
        try:
            message = json.loads(line)
        except ValueError:
            response = {"jsonrpc": "2.0", "id": None,
                        "error": {"code": -32700, "message": "parse error"}}
        else:
            response = adapter.handle(message)
        if response is not None:
            stdout.write(json.dumps(response) + "\n")
            stdout.flush()


def call_once(adapter, request_text, out, stdout):
    """--call: one request, its JSON result on stdout, its PNG (if any) saved to `out`."""
    try:
        request = json.loads(request_text)
        result, png = adapter.run(request["method"], request.get("params") or {})
    except (EmulatorError, KeyError, ValueError, TypeError, AttributeError) as error:
        message = f"missing {error} in the request" if isinstance(error, KeyError) else str(error)
        stdout.write(json.dumps({"error": message}) + "\n")
        return 1
    if png is not None:
        if out:
            with open(out, "wb") as file:
                file.write(png)
            result["png"] = os.path.abspath(out).replace("\\", "/")
        else:
            result["png"] = "not saved: pass --out <path>"
    stdout.write(json.dumps(result, indent=1) + "\n")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", default=os.environ.get("LICHII_DEBUG_SERVER", DEFAULT_SERVER),
                        help=f"host:port of EDEN_NX2_DEBUG_SERVER (default {DEFAULT_SERVER})")
    parser.add_argument("--call", metavar="JSON",
                        help='run one request and exit, e.g. \'{"method":"screenshot","params":{}}\'')
    parser.add_argument("--out", metavar="PNG", help="with --call: where to save the PNG")
    args = parser.parse_args()
    adapter = Adapter(Emulator(args.server))
    if args.call is not None:
        sys.exit(call_once(adapter, args.call, args.out, sys.stdout))
    serve(adapter, sys.stdin, sys.stdout)


if __name__ == "__main__":
    main()
