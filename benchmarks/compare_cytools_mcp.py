"""Compare the two shipped echo examples over stdio using the same official MCP client.

Use a Python environment with the CyToolsCore dependencies and mcp==1.30.0:
  python benchmarks/compare_cytools_mcp.py --core ../CyToolsCore --cpp build/cymcp_example.exe
"""

import argparse
import asyncio
from datetime import timedelta
from importlib.metadata import version
import json
import math
from pathlib import Path
import platform
from statistics import median
import sys
import tempfile
from time import perf_counter
import tomllib

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def elapsed_ms(start):
    return (perf_counter() - start) * 1000


async def echo(session, backend, payload):
    python_server = backend == "CyToolsCore"
    name = "op_echo" if python_server else "echo"
    inputs = {"text": payload}
    result = await session.call_tool(name, {"parameters": inputs} if python_server else inputs)
    polls = 0
    if result.isError:
        raise RuntimeError(f"{name} returned an error")
    value = json.loads(result.content[0].text)
    if python_server:
        while value["state"] not in ("Completed", "Cancelled", "Failed"):
            result = await session.call_tool("cy_get_job", {"jobId": value["jobId"]})
            if result.isError:
                raise RuntimeError("cy_get_job returned an error")
            polls += 1
            value = json.loads(result.content[0].text)
        if value["state"] != "Completed":
            raise RuntimeError(f"Echo job ended in {value['state']}")
        value = value["outputs"]
    if value != inputs:
        raise RuntimeError(f"{backend} returned unexpected echo contents")
    return polls


async def measure(backend, params, pings, echoes):
    with tempfile.TemporaryDirectory(prefix="cymcp-comparison-") as folder:
        server = params.model_copy(update={"cwd": folder})
        started = perf_counter()
        async with asyncio.timeout(120), stdio_client(server) as streams:
            async with ClientSession(*streams, read_timeout_seconds=timedelta(seconds=30)) as session:
                init = await session.initialize()
                ready = elapsed_ms(started)
                await session.list_tools()
                await session.send_ping()
                await echo(session, backend, "warm-up")
                sample = {"protocol": init.protocolVersion, "server_version": init.serverInfo.version,
                          "ready_ms": ready,
                          "ping_ms": [], "echo_ms": [], "job_polls": []}
                for _ in range(pings):
                    started = perf_counter()
                    await session.send_ping()
                    sample["ping_ms"].append(elapsed_ms(started))
                for i in range(echoes):
                    payload = f"local echo {i:06d}"
                    started = perf_counter()
                    polls = await echo(session, backend, payload)
                    sample["echo_ms"].append(elapsed_ms(started))
                    sample["job_polls"].append(polls)
                return sample


def distribution(values):
    ordered = sorted(values)
    return {"n": len(values), "min": round(ordered[0], 3), "p50": round(median(ordered), 3),
            "p95": round(ordered[math.ceil(0.95 * len(ordered)) - 1], 3),
            "max": round(ordered[-1], 3)}


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True, type=Path, help="CyToolsCore source checkout")
    parser.add_argument("--cpp", required=True, type=Path, help="Built C++ cymcp_example")
    parser.add_argument("--pairs", type=int, default=6)
    parser.add_argument("--pings", type=int, default=32)
    parser.add_argument("--echoes", type=int, default=6)
    args = parser.parse_args()
    if min(args.pairs, args.pings, args.echoes) <= 0:
        parser.error("Sample counts must be positive")
    core = args.core.resolve(strict=True)
    cpp = args.cpp.resolve(strict=True)
    core_version = tomllib.loads((core / "pyproject.toml").read_text("utf-8"))["project"]["version"]
    servers = {
        "CyToolsCore": StdioServerParameters(
            command=sys.executable, args=["-B", "-m", "cytool_test", "--data-dir", "data", "mcp"],
            env={"PYTHONPATH": str(core), "PYTHONIOENCODING": "utf-8"},
        ),
        "CyMCPSDK": StdioServerParameters(command=str(cpp), args=["mcp"]),
    }
    runs = []
    for i in range(args.pairs):
        order = list(servers) if i % 2 == 0 else list(reversed(servers))
        for backend in order:
            values = await measure(backend, servers[backend], args.pings, args.echoes)
            runs.append({"pair": i + 1, "backend": backend, **values})
    if len({r["protocol"] for r in runs}) != 1:
        raise RuntimeError("Servers negotiated different MCP protocol versions")
    summary = {}
    for backend in servers:
        selected = [r for r in runs if r["backend"] == backend]
        summary[backend] = {
            "ready_ms": distribution([r["ready_ms"] for r in selected]),
            "ping_ms": distribution([v for r in selected for v in r["ping_ms"]]),
            "echo_ms": distribution([v for r in selected for v in r["echo_ms"]]),
        }
    print(json.dumps({"os": platform.platform(), "python": platform.python_version(),
                      "official_mcp": version("mcp"), "core_version": core_version,
                      "cpp": str(cpp), "core": str(core), "summary": summary, "runs": runs},
                     indent=2))


if __name__ == "__main__":
    asyncio.run(main())
