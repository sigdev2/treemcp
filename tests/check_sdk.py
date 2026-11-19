"""Optional compatibility check using the official MCP Python SDK. Pass the server executable."""

import asyncio
import json
from pathlib import Path
import sys

from jsonschema import Draft202012Validator
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def check(executable: Path) -> None:
    async with stdio_client(StdioServerParameters(command=str(executable.resolve()))) as streams:
        async with ClientSession(*streams) as client:
            handshake = await client.initialize()
            assert handshake.protocolVersion == "2025-11-25"
            await client.send_ping()
            catalog = await client.list_tools()
            assert [tool.name for tool in catalog.tools] == ["languages"]
            tool = catalog.tools[0]
            Draft202012Validator.check_schema(tool.inputSchema)
            inputs = Draft202012Validator(tool.inputSchema)
            assert inputs.is_valid({}) and not inputs.is_valid({"extra": True})
            result = await client.call_tool(tool.name, {})
            assert not result.isError
            Draft202012Validator.check_schema(tool.outputSchema)
            Draft202012Validator(tool.outputSchema).validate(result.structuredContent)
            assert json.loads(result.content[0].text) == result.structuredContent
            assert (await client.call_tool(tool.name, {"extra": True})).isError


if __name__ == "__main__":
    asyncio.run(check(Path(sys.argv[1])))
