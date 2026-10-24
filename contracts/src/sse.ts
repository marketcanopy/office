// Gateway stream (SPEC.md 8.3): assembles a `text/event-stream` body and
// checks the event order both AI surfaces depend on.

export interface AssembledToolCall {
  id: string;
  name: string;
  arguments: string;
}

export interface AssembledChoice {
  content: string;
  reasoning: string;
  toolCalls: AssembledToolCall[];
  finishReason: string | null;
}

export type StreamResult =
  | { ok: true; choices: AssembledChoice[]; usage: Record<string, unknown> | null; done: true }
  | { ok: true; choices: AssembledChoice[]; usage: null; done: false; error: { code: string; type: string } }
  | { ok: false; violation: string; event: number };

const FINISH_REASONS = new Set(['stop', 'length', 'tool_calls', 'content_filter']);

type Json = Record<string, unknown>;

function isObject(value: unknown): value is Json {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** Splits an SSE body into the `data:` payload of each event. */
export function sseEvents(body: string): string[] {
  const events: string[] = [];
  for (const block of body.replace(/\r\n/g, '\n').split('\n\n')) {
    const data = block
      .split('\n')
      .filter((line) => line.startsWith('data:'))
      .map((line) => line.slice(5).replace(/^ /, ''));
    if (data.length > 0) events.push(data.join('\n'));
  }
  return events;
}

export function assembleStream(body: string): StreamResult {
  const events = sseEvents(body);
  const choices: AssembledChoice[] = [];
  const finished = new Set<number>();
  const firstSeen = new Set<number>();
  let usage: Json | null = null;
  const fail = (violation: string, event: number): StreamResult => ({ ok: false, violation, event });

  for (let i = 0; i < events.length; i += 1) {
    const raw = events[i] ?? '';
    if (raw === '[DONE]') {
      if (i !== events.length - 1) return fail('events after [DONE]', i);
      if (choices.length === 0) return fail('no choice before [DONE]', i);
      if (choices.some((_, index) => !finished.has(index))) return fail('choice without finish_reason', i);
      return { ok: true, choices, usage, done: true };
    }
    let event: unknown;
    try {
      event = JSON.parse(raw);
    } catch {
      return fail('event is not JSON', i);
    }
    if (!isObject(event)) return fail('event is not an object', i);

    if (isObject(event['error'])) {
      if (i !== events.length - 1) return fail('events after error', i);
      const error = event['error'];
      if (typeof error['code'] !== 'string' || typeof error['type'] !== 'string') return fail('error without code and type', i);
      return { ok: true, choices, usage: null, done: false, error: { code: error['code'], type: error['type'] } };
    }

    if (event['object'] !== 'chat.completion.chunk') return fail('not a chat.completion.chunk', i);
    const list = event['choices'];
    if (!Array.isArray(list)) return fail('choices missing', i);

    if (list.length === 0) {
      if (!isObject(event['usage'])) return fail('empty choices without usage', i);
      if (usage !== null) return fail('usage sent twice', i);
      usage = event['usage'];
      continue;
    }
    if (usage !== null) return fail('choice after usage', i);

    for (const item of list) {
      if (!isObject(item) || typeof item['index'] !== 'number' || !isObject(item['delta'])) return fail('malformed choice', i);
      const index = item['index'];
      const delta = item['delta'];
      if (finished.has(index)) return fail('choice continued after finish_reason', i);
      const choice = (choices[index] ??= { content: '', reasoning: '', toolCalls: [], finishReason: null });

      if (!firstSeen.has(index)) {
        firstSeen.add(index);
        if (delta['role'] !== 'assistant') return fail('first delta lacks role assistant', i);
      } else if (delta['role'] !== undefined) {
        return fail('role repeated', i);
      }

      const finish = item['finish_reason'];
      if (finish !== null && finish !== undefined) {
        if (typeof finish !== 'string' || !FINISH_REASONS.has(finish)) return fail('unknown finish_reason', i);
        // The agent stops reading a choice at finish_reason, dropping its delta.
        const carries = Object.keys(delta).some((key) => key !== 'role' && delta[key] !== null && delta[key] !== '');
        if (carries) return fail('finish_reason event carries content', i);
        if (finish === 'tool_calls' && choice.toolCalls.length === 0) return fail('tool_calls finish without a tool call', i);
        choice.finishReason = finish;
        finished.add(index);
        continue;
      }

      if (typeof delta['content'] === 'string') choice.content += delta['content'];
      if (typeof delta['reasoning_content'] === 'string') choice.reasoning += delta['reasoning_content'];
      const calls = delta['tool_calls'];
      if (calls !== undefined) {
        if (!Array.isArray(calls)) return fail('tool_calls not an array', i);
        for (const call of calls) {
          if (!isObject(call) || typeof call['index'] !== 'number') return fail('tool call without index', i);
          const fn = isObject(call['function']) ? call['function'] : {};
          const existing = choice.toolCalls[call['index']];
          if (existing === undefined) {
            if (call['index'] !== choice.toolCalls.length) return fail('tool call index skipped', i);
            if (typeof call['id'] !== 'string' || call['type'] !== 'function' || typeof fn['name'] !== 'string' || fn['arguments'] !== '') {
              return fail('first tool call delta incomplete', i);
            }
            choice.toolCalls.push({ id: call['id'], name: fn['name'], arguments: '' });
          } else {
            if (call['id'] !== undefined || fn['name'] !== undefined) return fail('tool call id or name repeated', i);
            if (typeof fn['arguments'] !== 'string') return fail('tool call fragment without arguments', i);
            existing.arguments += fn['arguments'];
          }
        }
      }
    }
  }
  return fail('stream ended without [DONE] or error', events.length);
}
