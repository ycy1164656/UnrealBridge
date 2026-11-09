"""Disk evidence checks only. A catalog snapshot cannot certify Editor memory."""
import hashlib
import re
from pathlib import Path


def revision_matches(revisions, root):
    problems, checked = [], 0
    if not isinstance(revisions, list):
        return {'state': 'unknown', 'checked_files': 0,
                'problems': [{'state': 'unknown', 'reason': 'invalid_revision_list'}]}
    for revision in revisions:
        if not isinstance(revision, dict):
            problems.append({'state': 'unknown', 'reason': 'invalid_revision_record'})
            continue
        name, expected = revision.get('file'), revision.get('sha256')
        if not isinstance(name, str) or not name or not isinstance(expected, str) or not re.fullmatch(r'[0-9a-fA-F]{64}', expected):
            problems.append({'file': name, 'state': 'unknown', 'reason': 'incomplete_revision'})
            continue
        path = Path(name)
        path = path if path.is_absolute() else root / path
        try:
            with path.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        except FileNotFoundError:
            problems.append({'file': name, 'state': 'missing'})
            continue
        except OSError as error:
            problems.append({'file': name, 'state': 'unknown', 'reason': type(error).__name__})
            continue
        checked += 1
        if actual != expected.lower():
            problems.append({'file': name, 'state': 'changed'})
    stale = any(p['state'] in ('changed', 'missing') for p in problems)
    return {'state': 'stale' if stale else 'unknown' if problems or not checked else 'current',
            'checked_files': checked, 'problems': problems}


def observation_validity(asset, controls, root):
    """Retain reviewed history and expose the limits of each recorded scope."""
    evidence = asset.get('observation_evidence', {})
    legacy_evidence = not isinstance(evidence, dict)
    if legacy_evidence:
        evidence = {}
    revision = asset.get('revision', {})
    if not isinstance(revision, dict):
        revision = {}
    source = revision_matches([{'file': asset.get('file'),
                               'sha256': revision.get('value') if revision.get('kind') == 'sha256' else None}], root)
    dependencies = revision_matches(evidence.get('dependency_revisions', []), root)
    preview = revision_matches(evidence.get('preview_revisions', []), root)
    coverage = evidence.get('dependency_coverage', {})
    if not isinstance(coverage, dict):
        coverage = {}
    coverage_state = coverage.get('state', 'unknown')
    if coverage_state not in ('complete', 'partial', 'unknown') or not isinstance(coverage.get('scope'), str) or not coverage.get('scope'):
        coverage_state = 'unknown'
    checks = [source, dependencies, preview, *(c['revision_check'] for c in controls)]
    reasons = [p for check in checks for p in check['problems']]
    if legacy_evidence:
        reasons.append({'state': 'unknown', 'reason': 'legacy_observation_reference_has_no_revision_inventory'})
    missing_controls = sorted(set(asset.get('control_record_ids', [])) - {c['record_id'] for c in controls})
    if missing_controls:
        reasons.append({'state': 'unknown', 'reason': 'declared_control_record_missing', 'record_ids': missing_controls})
    stale = asset.get('observation_stale', False) or any(c['state'] == 'stale' for c in checks)
    # Empty dependencies are legitimate only when a scoped inventory explicitly says none.
    dependencies_known = dependencies['state'] == 'current' or (
        evidence.get('dependency_revisions', []) == [] and coverage.get('no_dependencies') is True)
    complete = (not missing_controls and coverage_state == 'complete' and dependencies_known and source['state'] == 'current'
                and preview['state'] == 'current' and all(c['revision_check']['state'] == 'current' for c in controls))
    state = 'stale' if stale else 'current' if complete else 'unknown'
    if asset.get('observation_stale', False):
        reasons.append({'state': 'stale', 'reason': 'stored_stale_flag'})
    if not complete:
        reasons.append({'state': 'unknown', 'reason': 'dependency_or_preview_coverage_incomplete'})
    return {'historical_observation': asset.get('visual_state', 'unseen'),
            'disk_state': state, 'source_revision': source, 'dependency_revision': dependencies,
            'control_revisions': [{'record_id': c['record_id'], **c['revision_check']} for c in controls],
            'dependency_coverage': coverage_state, 'coverage_scope': coverage.get('scope'),
            'preview_config_state': preview['state'], 'reasons': reasons,
            'runtime_state': 'unknown', 'runtime_reason': 'read_only_disk_query_does_not_check_editor_memory',
            'current_acceptance_eligible': False}
