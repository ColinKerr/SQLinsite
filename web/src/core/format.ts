// Formats a byte count as KB / MB / GB, choosing the largest unit for which the
// value is >= 1 and keeping at most 4 total digits (integer + decimal). Trailing
// zeros are trimmed, e.g. 1536 → "1.5 KB", 4096 → "4 KB", 1048576 → "1 MB",
// 10.7e6 → "10.2 MB". The smallest unit shown is KB (page sizes are >= 512 B).
export function formatBytes(bytes: number): string {
  const units: [string, number][] = [
    ["GB", 1024 ** 3],
    ["MB", 1024 ** 2],
    ["KB", 1024],
  ];
  const [unit, size] = units.find(([, s]) => bytes >= s) ?? ["KB", 1024];
  const value = bytes / size;
  const intDigits = Math.max(1, Math.floor(Math.abs(value))).toString().length;
  const decimals = Math.max(0, Math.min(3, 4 - intDigits));
  // Trim trailing zeros (and a dangling dot), but only past a decimal point so
  // integers like "1000" are left intact.
  let text = value.toFixed(decimals);
  if (text.includes(".")) text = text.replace(/0+$/, "").replace(/\.$/, "");
  return `${text} ${unit}`;
}
