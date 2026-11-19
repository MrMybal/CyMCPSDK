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
    parser.add_argument("--gateway-server", help="Optional cymcp_mcp_optimization_tests executable")
    args = parser.parse_args()
    if args.gateway_server:
        async with stdio_client(StdioServerParameters(command=args.gateway_server, args=["--serve"])) as streams:
            async with ClientSession(*streams) as session:
                initialized = await session.initialize()
                assert initialized.capabilities.resources is not None
                names, cursor = set(), None
                while True:
                    page = await session.list_tools(cursor=cursor)
                    names.update(item.name for item in page.tools)
                    cursor = page.nextCursor
                    if cursor is None:
                        break
                assert len(names) == 5
                result = await session.call_tool("cy_tools_call", {"name": "large"})
                assert not result.isError and result.content[1].type == "resource_link"
                manifest = await session.read_resource(result.content[1].uri)
                uri = json.loads(manifest.contents[0].text)["firstUri"]
                pieces = []
                while uri:
                    part = await session.read_resource(uri)
                    pieces.append(part.contents[0].text)
                    uri = part.model_dump(by_alias=True)["_meta"]["com.cymcp/chunk"].get("nextUri")
                assert json.loads("".join(pieces))["text"] == "x" * 200000 + " é🙂"
                assert not (await session.list_resources()).resources
        print("Official MCP client passed paged gateway tools, resource links and complete UTF-8 chunks")
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
