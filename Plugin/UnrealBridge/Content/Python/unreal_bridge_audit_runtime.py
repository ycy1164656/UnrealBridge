"""Read-only bounded metric collection in the loaded Editor (no compile/save)."""
import unreal
from pathlib import Path


def move_readback(entry):
    source,destination=entry['source'],entry['destination']
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    src=registry.get_asset_by_object_path(source+'.'+source.rsplit('/',1)[1])
    dst=registry.get_asset_by_object_path(destination+'.'+destination.rsplit('/',1)[1])
    target=unreal.load_asset(destination)
    kind=str(src.asset_class_path.asset_name) if src.is_valid() else 'absent'
    resolved=unreal.load_asset(source) if src.is_valid() else None
    refs=[]
    for path in entry['referencers']:
        asset=unreal.load_asset(path)
        dependencies=list(unreal.UnrealBridgeAssetLibrary.get_package_dependencies(path,False))
        refs.append({'path':path,'loadable':asset is not None,'dependencies':dependencies,
                     'resolves':asset is not None and (destination in dependencies or (source in dependencies and resolved==target))})
    return {'ok':dst.is_valid() and target is not None and kind in ('absent','ObjectRedirector') and
             (kind=='absent' or resolved==target) and all(r['resolves'] for r in refs),
            'source_registry_class':kind,'redirector_retained':kind=='ObjectRedirector',
            'redirector_destination':resolved.get_path_name() if resolved else None,
            'destination':target.get_path_name() if target else None,'referencers':refs,
            'saved':False,'cleanup':'not requested; explicit reviewed fix-up only'}


def asset_metrics(paths, details_limit=64):
    if not isinstance(paths,list) or len(paths)>128:
        raise ValueError('Metrics accept at most 128 paths per batch')
    sizes=unreal.UnrealBridgeAssetLibrary.get_asset_disk_sizes_batch(paths)
    rows=[]
    for index,path in enumerate(paths):
        data=unreal.EditorAssetLibrary.find_asset_data(path)
        row={'asset':path,'exists':data.is_valid(),'editor_package_bytes':int(sizes[index]) if sizes[index]>=0 else None,
             'disk_source':'UE package file size; excludes source art and cooked compression',
             'detail_status':'not_loaded_budget','unknown':[]}
        if not row['exists']:
            row['unknown'].append('missing_or_unregistered');rows.append(row);continue
        row['class']=str(data.asset_class_path.asset_name)
        if index>=details_limit:
            rows.append(row);continue
        kind=row['class']
        row['detail_status']='observed'
        if kind in ('Texture2D','StaticMesh','SkeletalMesh','AnimSequence','SoundWave'):
            obj=unreal.load_asset(path)
            data=obj.get_editor_property('asset_import_data')
            files=list(data.extract_filenames()) if data else []
            row['source_art']=[]
            for filename in files[:8]:
                file=Path(filename)
                try:size=file.stat().st_size if file.is_file() else None
                except OSError:size=None
                row['source_art'].append({'path':str(file),'bytes':size,'status':'observed_file_size' if size is not None else 'missing_or_unavailable'})
            if len(files)>8:row['unknown'].append('source_art_list_truncated')
        if kind in ('StaticMesh','SkeletalMesh'):
            info=(unreal.UnrealBridgeAssetLibrary.get_static_mesh_info(path) if kind=='StaticMesh' else unreal.UnrealBridgeAssetLibrary.get_skeletal_mesh_info(path))
            row['mesh']={'lods':int(info.num_lo_ds),'lod_stats':[{'vertices':int(s.vertex_count),'triangles':int(s.triangle_count)} for s in info.lod_stats],
                         'material_paths':list(info.material_asset_paths),'bounds_extent':[info.bounds_extent.x,info.bounds_extent.y,info.bounds_extent.z]}
            if kind=='StaticMesh':
                row['mesh'].update(uv_channels=int(info.num_uv_channels),has_collision=bool(info.has_collision),nanite=bool(info.has_nanite_data))
        elif kind=='Texture2D':
            info=unreal.UnrealBridgeAssetLibrary.get_texture_info(path)
            row['texture']={k:info.get_editor_property(k) for k in ('width','height','num_mips','pixel_format','compression_settings','lod_group','srgb','never_stream')}
            row['texture']['estimated_resource_bytes']=int(info.resource_size_bytes)
            row['texture']['memory_basis']='GetResourceSize estimate; not measured resident memory'
        elif kind in ('Material','MaterialInstanceConstant'):
            row['unknown'].extend(['shader_instruction_cost_not_measured','overdraw_not_measured'])
        elif kind.endswith('Blueprint'):
            row['compile_status']='not_compiled_by_readonly_audit'
        else:
            row['detail_status']='registry_only'
        rows.append(row)
    return {'rows':rows,'mutations':'none','detail_limit_per_batch':details_limit}
