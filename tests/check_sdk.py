"""Optional compatibility check using the official MCP Python SDK. Pass the server executable."""

import asyncio
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from jsonschema import Draft202012Validator
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def check(executable: Path, workspace: Path) -> None:
    server = StdioServerParameters(command=str(executable.resolve()), args=["--workspace-root", str(workspace)])
    async with stdio_client(server) as streams:
        async with ClientSession(*streams) as client:
            handshake = await client.initialize()
            assert handshake.protocolVersion == "2025-11-25"
            await client.send_ping()
            catalog = await client.list_tools()
            query = '((function_definition name: (identifier) @name) (#eq? @name "answer"))'
            source = "def answer():\n    return 42\n"
            (workspace / "example.py").write_text(source, encoding="utf-8")
            examples = {
                "languages": [{}],
                "query_validate": [
                    {"language": "python", "query": query},
                    {"language": "python", "query": "(unknown_node)"},
                ],
                "syntax_tree": [{"language": "python", "source": source}, {"language": "python", "path": "example.py"}],
                "structural_search": [
                    {"language": "python", "source": source, "query": query},
                    {"language": "python", "path": "example.py", "query": query},
                    {"language": "python", "paths": ["."], "include": ["*.py"], "query": query},
                ],
            }
            assert [tool.name for tool in catalog.tools] == list(examples)
            for tool in catalog.tools:
                Draft202012Validator.check_schema(tool.inputSchema)
                Draft202012Validator.check_schema(tool.outputSchema)
                inputs = Draft202012Validator(tool.inputSchema)
                outputs = Draft202012Validator(tool.outputSchema)
                for args in examples[tool.name]:
                    inputs.validate(args)
                    result = await client.call_tool(tool.name, args)
                    assert not result.isError
                    outputs.validate(result.structuredContent)
                    assert json.loads(result.content[0].text) == result.structuredContent
                    if tool.name == "structural_search":
                        capture, = result.structuredContent["matches"][0]["captures"]
                        assert capture["name"] == "name" and capture["text"] == "answer"
                        assert (capture["range"]["startByte"], capture["range"]["endByte"]) == (4, 10)
                invalid = {**examples[tool.name][0], "extra": True}
                assert not inputs.is_valid(invalid)
                assert (await client.call_tool(tool.name, invalid)).isError


if __name__ == "__main__":
    with TemporaryDirectory() as directory:
        asyncio.run(check(Path(sys.argv[1]), Path(directory)))
