"""Read-only material export fallbacks for demonstrated VFX control gaps.

Run under ordinary Python; no writes to Unreal assets. Keep native graph-node GUID
and serialized MaterialExpressionGuid distinct. Object path is the identity.
"""
import math,re

def material_objects(text):
    stack=[];result=[]
    for line in text.splitlines():
        if line.lstrip().startswith('Begin Object'):
            name=re.search(r'Name="([^"]+)"',line);path=re.search(r'ExportPath="([^"]+)"',line)
            stack.append({'name':name.group(1) if name else None,'object_path':path.group(1) if path else None,'lines':[]})
        elif line.strip()=='End Object':
            if stack:
                item=stack.pop()
                if item['object_path']:
                    item['properties']={m.group(1):m.group(2) for s in item.pop('lines') if (m:=re.match(r'\s*([\w.]+)=(.*)',s))}
                    result.append(item)
        elif stack:stack[-1]['lines'].append(line)
    return result

def field_value(obj,field):
    value=obj.get('properties',{}).get(field)
    return {'state':'read','value':value} if value is not None else {'state':'unknown','value':None}

def duplicate_expression_guids(objects):
    identities={}
    for obj in objects:
        guid=obj.get('properties',{}).get('MaterialExpressionGuid')
        if guid:identities.setdefault(guid,[]).append(obj['object_path'])
    return {g:paths for g,paths in identities.items() if len(paths)>1}

def reconcile_setter(returned,*,parameter_exists,effective_value,expected_value,explicit_override):
    if not parameter_exists or effective_value is None:
        state='unknown'
    elif isinstance(effective_value,(int,float)) and isinstance(expected_value,(int,float)):
        state='verified' if explicit_override and math.isclose(effective_value,expected_value,rel_tol=1e-6,abs_tol=1e-6) else 'mismatch'
    else:state='verified' if explicit_override and effective_value==expected_value else 'mismatch'
    return {'state':state,'setter_return':returned,'effective_value':effective_value,'explicit_override':explicit_override}
