import { ZoomControls } from "./ZoomControls.tsx";
import { SessionInfo } from "./SessionInfo.tsx";

// Secondary bar shown for the Pages/Tables views: zoom controls and session/map
// information, right aligned. The profile Overlay control lives in the shared top
// bar (see TopBar); the Block view has its own controls bar (see BlockControls).
export function PageTopBar() {
  return (
    <div className="page-top-bar">
      <div className="bar-spacer" />
      <ZoomControls />
      <SessionInfo />
    </div>
  );
}
