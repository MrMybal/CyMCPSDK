"""Optional development interop test. Python is NOT a dependency of the C++ SDK."""
import asyncio
import json
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main():
    async with stdio_client(StdioServerParameters(command=sys.argv[1], args=["mcp"])) as streams:
        async with ClientSession(*streams) as session:
            await session.initialize()
            tools = await session.list_tools()
            assert {item.name for item in tools.tools} == {"echo", "sum"}
            result = await session.call_tool("sum", {"a": 17, "b": 25})
            assert not result.isError
            assert json.loads(result.content[0].text)["value"] == 42
            invalid = await session.call_tool("sum", {"a": "invalid", "b": 25})
            assert invalid.isError
            print("Official MCP client: initialize, discovery, invocation and invalid arguments passed")


asyncio.run(main())
