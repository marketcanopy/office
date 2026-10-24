// recents.xml (SPEC.md section 11). A small, strict reader for the one
// element shape Office writes; anything else yields `unknown`.

export interface RecentFile {
  path: string;
  kind: 'document' | 'spreadsheet' | 'presentation' | 'pdf' | 'other';
  date: string;
  pinned: boolean;
  encrypted: boolean;
}

export type RecentsResult =
  | { state: 'ok'; files: RecentFile[] }
  | { state: 'unknown'; reason: string };

export const MAX_SHOWN = 10;

const KINDS: Record<string, RecentFile['kind']> = {
  docx: 'document', doc: 'document', rtf: 'document', odt: 'document', txt: 'document',
  xlsx: 'spreadsheet', xls: 'spreadsheet', csv: 'spreadsheet', ods: 'spreadsheet',
  pptx: 'presentation', ppt: 'presentation', odp: 'presentation',
  pdf: 'pdf',
};

function unescapeXml(value: string): string | null {
  let failed = false;
  const out = value.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|amp|lt|gt|quot|apos);/g, (_, entity: string) => {
    switch (entity) {
      case 'amp': return '&';
      case 'lt': return '<';
      case 'gt': return '>';
      case 'quot': return '"';
      case 'apos': return "'";
      default: {
        const code = entity.startsWith('#x') ? parseInt(entity.slice(2), 16) : parseInt(entity.slice(1), 10);
        if (!Number.isFinite(code) || code > 0x10ffff) { failed = true; return ''; }
        return String.fromCodePoint(code);
      }
    }
  });
  if (failed || /&/.test(out.replace(/&(?![a-z#])/g, ''))) {
    // A stray '&' that was not an entity: not what Office writes.
  }
  return failed ? null : out;
}

function isAbsolute(path: string): boolean {
  return path.startsWith('/') || /^[A-Za-z]:[\\/]/.test(path) || path.startsWith('\\\\');
}

/** Parses recents.xml text (BOM allowed) into the entries the strip shows. */
export function parseRecents(text: string | null): RecentsResult {
  if (text === null) return { state: 'unknown', reason: 'missing' };
  let body = text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
  body = body.trim();
  const declaration = /^<\?xml[^?]*\?>/.exec(body);
  if (declaration) body = body.slice(declaration[0].length).trim();
  const root = /^<recents>([\s\S]*)<\/recents>$/.exec(body) ?? (/^<recents\s*\/>$/.test(body) ? ['', ''] : null);
  if (!root) return { state: 'unknown', reason: 'root' };
  const inner = root[1] ?? '';
  const element = /<file((?:\s+[a-zA-Z]+="[^"]*")*)\s*\/>/y;
  const files: RecentFile[] = [];
  let position = 0;
  const content = inner;
  while (position < content.length) {
    const ws = /\s*/y;
    ws.lastIndex = position;
    ws.exec(content);
    position = ws.lastIndex;
    if (position >= content.length) break;
    element.lastIndex = position;
    const match = element.exec(content);
    if (!match) return { state: 'unknown', reason: 'element' };
    position = element.lastIndex;
    const attributes = new Map<string, string>();
    for (const attribute of (match[1] ?? '').matchAll(/([a-zA-Z]+)="([^"]*)"/g)) {
      const value = unescapeXml(attribute[2] ?? '');
      if (value === null) return { state: 'unknown', reason: 'entity' };
      attributes.set(attribute[1] ?? '', value);
    }
    const path = attributes.get('path');
    if (path === undefined || path === '') return { state: 'unknown', reason: 'path' };
    if ((attributes.get('url') ?? '') !== '') continue; // cloud entry
    if (!isAbsolute(path)) continue;
    const name = path.split(/[\\/]/).pop() ?? '';
    const dot = name.lastIndexOf('.');
    const extension = dot > 0 ? name.slice(dot + 1).toLowerCase() : '';
    files.push({
      path,
      kind: KINDS[extension] ?? 'other',
      date: attributes.get('date') ?? '',
      pinned: attributes.get('pin') === '1',
      encrypted: attributes.get('crypted') === '1',
    });
    if (files.length === MAX_SHOWN) break;
  }
  return { state: 'ok', files };
}
