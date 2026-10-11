let timer: ReturnType<typeof setInterval> | null = null;

self.onmessage = (event: MessageEvent<{ intervalMs?: unknown }>) => {
  const intervalMs = event.data?.intervalMs;
  if (typeof intervalMs !== 'number' || !Number.isFinite(intervalMs) || intervalMs <= 0) return;
  if (timer !== null) clearInterval(timer);
  timer = setInterval(() => self.postMessage('tick'), intervalMs);
};
