import { ZoomControls } from "./ZoomControls.tsx";
import { SessionInfo } from "./SessionInfo.tsx";
import { BlockControls } from "./BlockControls.tsx";

// Secondary bar shown for the canvas views (Pages, Tables, Blocks): the Block-view
// color-mode selector (Blocks only), zoom controls, and session/map information,
// right aligned. The profile Overlay control lives in the shared top bar (see
// TopBar) so it is reachable from every view.
export function PageTopBar() {
  return (
    <div className="page-top-bar">
      <BlockControls />
      <div className="bar-spacer" />
      <ZoomControls />
      <SessionInfo />
    </div>
  );
}
