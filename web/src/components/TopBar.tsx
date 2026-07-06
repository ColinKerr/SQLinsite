import { ViewPicker } from "./ViewPicker.tsx";
import { ZoomControls } from "./ZoomControls.tsx";
import { ProfileControls } from "./ProfileControls.tsx";
import { SessionInfo } from "./SessionInfo.tsx";

// Groups: App info · View picker (left) — Zoom · Profile · Session info (right).
export function TopBar() {
  return (
    <header>
      <div className="bar-group" id="app-info">
        <h1>SQLinsite</h1>
      </div>
      <ViewPicker />
      <div className="bar-spacer" />
      <ZoomControls />
      <ProfileControls />
      <SessionInfo />
    </header>
  );
}
