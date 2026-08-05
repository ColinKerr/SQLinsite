import { ViewPicker } from "./ViewPicker.tsx";
import { HistoryNav } from "./HistoryNav.tsx";
import { ProfileControls } from "./ProfileControls.tsx";

// Top bar: app info + the view picker (left aligned), then the global profile
// Overlay control and the history navigator (right aligned). The Overlay control
// lives here — not in the per-view Page Top Bar — so the profile metric + source
// selection is shared across every view that shades by profile.
export function TopBar() {
  return (
    <header>
      <div className="bar-group" id="app-info">
        <h1>SQLinsite</h1>
      </div>
      <ViewPicker />
      <ProfileControls />
      <HistoryNav />
    </header>
  );
}
