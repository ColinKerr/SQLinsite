import { ViewPicker } from "./ViewPicker.tsx";

// Top bar: app info + the view picker (left aligned). Page-specific controls
// (zoom, profile, session info) live in the Page Top Bar, shown per Page View.
export function TopBar() {
  return (
    <header>
      <div className="bar-group" id="app-info">
        <h1>SQLinsite</h1>
      </div>
      <ViewPicker />
    </header>
  );
}
