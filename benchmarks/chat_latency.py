"""Compare local clients with an already installed Ollama model.

Start a local Ollama service, then run e.g.
    python benchmarks/chat_latency.py build/cymcp_chat.exe --model qwen3:0.6b
    python benchmarks/chat_latency.py --chat-lab build/cymcp_chat_app.exe --ollama-cli ollama.exe --model qwen3:0.6b
Only the synthetic prompt below is sent. Results contain timings and lengths.
"""

import argparse
import http.client
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import threading
from time import perf_counter
from urllib.parse import urlsplit


PROMPT = "Reply with only the single word OK."
MAX_TOKENS = 96
TIMEOUT = 90


def sample(start, first, end, response):
    if first is None:
        raise RuntimeError("The model completed without visible text")
    return {
        "first_text_ms": round(1000 * (first - start), 2),
        "total_ms": round(1000 * (end - start), 2),
        "answer_chars": len(response),
        "expected_answer": response.strip().upper().rstrip(".") == "OK",
    }


def sdk_turn(binary, config):
    with subprocess.Popen(
        [str(binary), str(config)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, cwd=config.parent,
    ) as child:
        timeout = threading.Timer(TIMEOUT, child.kill)
        timeout.start()
        try:
            for line in child.stdout:
                event = json.loads(line)
                if (event["type"] == "ConnectionStage"
                        and event["data"]["code"] == "SessionStart"):
                    break
            else:
                raise RuntimeError("SDK exited before opening the Ollama session")
            message = json.dumps({"text": PROMPT}).encode() + b"\n"
            first = None
            parts = []
            start = perf_counter()
            child.stdin.write(message)
            child.stdin.flush()
            for line in child.stdout:
                event = json.loads(line)
                if event["type"] == "TextDelta":
                    part = event["data"]["text"]
                    if first is None and part.strip():
                        first = perf_counter()
                    parts.append(part)
                elif event["type"] == "TurnCompleted":
                    return sample(start, first, perf_counter(), "".join(parts))
                elif event["type"] in ("Error", "TurnFailed"):
                    raise RuntimeError("SDK generation failed")
            raise RuntimeError("SDK ended without a final answer")
        finally:
            timeout.cancel()
            child.stdin.close()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()


def native_turn(authority, model):
    body = json.dumps({"model": model, "stream": True, "think": False,
                       "options": {"num_predict": MAX_TOKENS},
                       "messages": [{"role": "user", "content": PROMPT}]}).encode()
    connection = http.client.HTTPConnection(authority, timeout=TIMEOUT)
    try:
        first = None
        parts = []
        start = perf_counter()
        connection.request("POST", "/api/chat", body, {"Content-Type": "application/json"})
        response = connection.getresponse()
        if response.status != 200:
            raise RuntimeError(f"Ollama returned HTTP {response.status}")
        for line in response:
            event = json.loads(line)
            part = event.get("message", {}).get("content", "")
            if first is None and part.strip():
                first = perf_counter()
            parts.append(part)
            if event.get("done"):
                return sample(start, first, perf_counter(), "".join(parts))
        raise RuntimeError("Ollama ended without a final answer")
    finally:
        connection.close()


def chat_lab_turn(binary, config):
    work = config.parent
    plan = work / "exercise.json"
    plan.write_text(json.dumps({"steps": [
        {"action": "connect"}, {"action": "page", "index": 0},
        {"action": "send", "text": PROMPT, "measure_frames": True, "settle_frames": 1,
         "reply": "OK", "trim_reply": True},
    ]}), encoding="utf-8")
    report = work / "exercise-report.json"
    report.unlink(missing_ok=True)
    run = subprocess.run(
        [binary, "--hidden", "--config", config, "--exercise", plan, "--report", report,
         "--log-file", work / "chat.log"], cwd=work, timeout=TIMEOUT, check=False,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    if not report.exists():
        raise RuntimeError(f"Chat Lab exited without a report (code {run.returncode})")
    result = json.loads(report.read_text(encoding="utf-8"))
    turn = result["results"][-1]
    if not result["passed"] or run.returncode:
        raise RuntimeError(f"Chat Lab failed at {turn['action']} (code {turn['error']!r})")
    return {"first_text_ms": round(turn["first_rendered_ms"], 2),
            "total_ms": round(turn["completed_frame_ms"], 2),
            "answer_chars": turn["reply_bytes"], "expected_answer": turn["passed"]}


def ollama_cli_turn(binary, authority, model):
    env = {**os.environ, "OLLAMA_HOST": "http://" + authority,
           "OLLAMA_NO_CLOUD": "true", "NO_COLOR": "1"}
    command = [binary, "run", "--think=false", "--hidethinking", "--nowordwrap", model, PROMPT]
    start = perf_counter()
    with subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, env=env, bufsize=0) as child:
        timeout = threading.Timer(TIMEOUT, child.kill)
        timeout.start()
        try:
            first = None
            chunks = bytearray()
            while char := child.stdout.read(1):
                if first is None and char.strip():
                    first = perf_counter()
                chunks.extend(char)
            end = perf_counter()
            if child.wait() != 0:
                raise RuntimeError("ollama run failed to generate with the installed model")
            reply = chunks.decode().strip()
            if reply != "OK":
                raise RuntimeError("ollama run did not return the requested single word OK")
            return sample(start, first, end, reply)
        finally:
            timeout.cancel()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path, nargs="?")
    parser.add_argument("--chat-lab", type=Path, help="Chat Lab executable (Win32/DX11)")
    parser.add_argument("--ollama-cli", help="Path or executable name of the native Ollama CLI")
    parser.add_argument("--endpoint", default="http://127.0.0.1:11434")
    parser.add_argument("--model", required=True, help="Exact name in the local Ollama catalogue")
    parser.add_argument("--pairs", type=int, default=6)
    args = parser.parse_args()
    url = urlsplit(args.endpoint)
    if (url.scheme != "http" or url.hostname not in ("localhost", "127.0.0.1")
            or url.username or url.password or url.query or url.fragment or url.path not in ("", "/")):
        parser.error("--endpoint must be a local HTTP service")
    if args.pairs < 2 or args.pairs % 2:
        parser.error("--pairs must be a positive even number for balanced order")
    if args.sdk and not (args.chat_lab or args.ollama_cli):
        sdk = args.sdk.resolve(strict=True)
    elif args.chat_lab and args.ollama_cli and not args.sdk:
        gui = args.chat_lab.resolve(strict=True)
        cli = shutil.which(args.ollama_cli)
        if not cli:
            parser.error("--ollama-cli must name an installed executable")
    else:
        parser.error("supply a cymcp_chat binary, or both --chat-lab and --ollama-cli")
    connection = http.client.HTTPConnection(url.netloc, timeout=TIMEOUT)
    try:
        connection.request("GET", "/api/tags")
        models = json.load(connection.getresponse())["models"]
        if args.model not in {model["name"] for model in models}:
            parser.error("The requested model must already be installed")
        connection.request("POST", "/api/show", json.dumps({"model": args.model}),
                           {"Content-Type": "application/json"})
        response = connection.getresponse()
        metadata = json.load(response)
        if response.status != 200 or metadata.get("remote_model") or metadata.get("remote_host"):
            parser.error("The benchmark requires local model weights")
        connection.request("GET", "/api/version")
        version = json.load(connection.getresponse())["version"]
    finally:
        connection.close()
    with tempfile.TemporaryDirectory() as directory:
        config = Path(directory) / "chat.json"
        settings = {"base_url": "http://" + url.netloc, "think": False}
        if args.sdk:
            settings["max_output_tokens"] = MAX_TOKENS
        config.write_text(json.dumps({
            "provider": "ollama", "model": args.model,
            "settings": settings,
        }), encoding="utf-8")
        if args.sdk:
            turns = {"sdk": lambda: sdk_turn(sdk, config),
                     "native": lambda: native_turn(url.netloc, args.model)}
            boundaries = {"sdk": "ready session input to first text delta / TurnCompleted",
                          "native": "HTTP request to first content / done event"}
        else:
            turns = {"chat_lab": lambda: chat_lab_turn(gui, config),
                     "ollama_cli": lambda: ollama_cli_turn(cli, url.netloc, args.model)}
            boundaries = {
                "chat_lab": "send in connected hidden Chat tab to first text / final GPU-rendered frame",
                "ollama_cli": "before process launch to first nonwhitespace stdout byte / stdout EOF",
            }
        for turn in turns.values():
            turn()  # Warm both paths. Every measured request starts a new conversation.
        pairs = []
        for index in range(args.pairs):
            order = list(turns) if index % 2 == 0 else list(reversed(turns))
            pairs.append({name: turns[name]() for name in order})
    medians = {name: {key: statistics.median(p[name][key] for p in pairs)
                      for key in ("first_text_ms", "total_ms")} for name in turns}
    print(json.dumps({"model": args.model, "ollama_version": version, "os": os.name,
                      "num_predict": settings.get("max_output_tokens"), "think": False,
                      "boundaries": boundaries, "pairs": pairs,
                      "medians": medians}, indent=2))


if __name__ == "__main__":
    main()
