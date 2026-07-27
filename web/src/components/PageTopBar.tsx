import { ZoomControls } from "./ZoomControls.tsx";
import { ProfileControls } from "./ProfileControls.tsx";
import { SessionInfo } from "./SessionInfo.tsx";

// Secondary bar shown only for the Page Views (Pages and Tables): zoom controls,
// profile controls, and session/map information, all right aligned.
export function PageTopBar() {
  return (
    <div className="page-top-bar">
      <div className="bar-spacer" />
      <ZoomControls />
      <ProfileControls />
      <SessionInfo />
    </div>
  );
}
