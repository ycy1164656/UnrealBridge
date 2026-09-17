"""Real MCP discovery for new/legacy tools; does not claim native execution."""
import argparse
import asyncio
import json
from pathlib import Path
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main():
    p=argparse.ArgumentParser(); p.add_argument('--out',required=True); a=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    params=StdioServerParameters(command=sys.executable,args=[str(root/'.claude/skills/unreal-bridge/scripts/unreal_bridge_mcp_server.py')],cwd=str(root))
    async with asyncio.timeout(45):
        async with stdio_client(params) as (reader,writer):
            async with ClientSession(reader,writer) as session:
                await session.initialize(); listing=await session.list_tools()
                tools={tool.name:tool for tool in listing.tools}
                required=['bridge_submit_upgrade_validation','bridge_submit_sr_scenario','bridge_control_network_session',
                          'bridge_external_session','bridge_submit_pointer_action','bridge_submit_authoring_request','bridge_submit_audio_mix_session']
                legacy=['bridge_call','bridge_runtime_run','bridge_runtime_status','bridge_runtime_cancel','bridge_submit_slate_action']
                assert all(name in tools for name in required+legacy),sorted(set(required+legacy)-tools.keys())
                for name in required:
                    schema=tools[name].inputSchema
                    assert schema.get('type')=='object' and schema.get('properties'),(name,schema)
                report={'status':'passed','scope':'actual_mcp_stdio_tools_list_only','tool_count':len(tools),
                        'new_tools':{name:tools[name].inputSchema for name in required},'legacy_tools_present':legacy,
                        'native_execution_verified':False,'assertions':['New upgrade tools are discoverable over MCP','Every new tool has an object input schema','Existing runtime/call/Slate tools remain discoverable']}
    out=Path(a.out); out.parent.mkdir(parents=True,exist_ok=True); out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'status':'passed','checks':len(report['assertions']),'mcp_tools':report['tool_count'],'native_execution_verified':False,'out':str(out)}))


if __name__=='__main__': asyncio.run(main())
