// Overlay fill drawn over a cell's base color when a profile overlay is active.
// Touched cells get a white tint that brightens with access intensity; cells the
// current profile never touched get a capped black veil that darkens them so the
// touched cells stand out. The veil is < 1, so an untouched cell is never drawn
// fully black — it keeps a fraction of its base color.
export const UNTOUCHED_DARKEN = 0.6;

export function overlayFill(v: number, globalMax: number): string {
  return v > 0
    ? `rgba(255,255,255,${(0.12 + (0.6 * v) / globalMax).toFixed(3)})`
    : `rgba(0,0,0,${UNTOUCHED_DARKEN})`;
}
