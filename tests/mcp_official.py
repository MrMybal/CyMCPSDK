"""Optional development interop test. Python is NOT a dependency of the C++ SDK."""
import argparse
import asyncio
import json
import os
import re
import secrets
import httpx
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.client.streamable_http import streamable_http_client


async def check(read, write):
    async with ClientSession(read, write) as session:
        await session.initialize()
        tools = await session.list_tools()
        assert {item.name for item in tools.tools} == {"echo", "sum"}
        result = await session.call_tool("sum", {"a": 17, "b": 25})
        assert not result.isError
        assert json.loads(result.content[0].text)["value"] == 42
        invalid = await session.call_tool("sum", {"a": "invalid", "b": 25})
        assert invalid.isError


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", help="Path to the built cymcp_example executable")
    parser.add_argument("--transport", choices=["both", "stdio", "http"], default="both")
    args = parser.parse_args()
    if args.transport in ("both", "stdio"):
        async with stdio_client(StdioServerParameters(command=args.server, args=["mcp"])) as streams:
            await check(*streams)
        print("Official MCP client passed over stdio")
    if args.transport == "stdio":
        return
    token = secrets.token_hex(32)
    server = await asyncio.create_subprocess_exec(
        args.server, "serve", "0", env={**os.environ, "CYMCP_TOKEN": token},
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        line = await asyncio.wait_for(server.stderr.readline(), 10)
        address = re.search(rb"http://127\.0\.0\.1:[0-9]+", line)
        if not address:
            raise RuntimeError("The SDK example did not start its local server")
        async with httpx.AsyncClient(headers={"Authorization": "Bearer " + token}, trust_env=False) as http:
            async with streamable_http_client(address.group().decode() + "/mcp", http_client=http) as streams:
                await check(*streams[:2])
        print("Official MCP client passed over HTTP")
    finally:
        if server.returncode is None:
            server.terminate()
        await server.wait()


asyncio.run(main())
