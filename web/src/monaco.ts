// Slim Monaco: the editor core + the SQL language only (no JSON/TS/CSS/HTML
// language services or the full basic-languages set), configured to use the
// *bundled* copy (no CDN) and its editor web worker. Keeps the bundle small and
// self-contained for embedding in the binary.
import * as monaco from "monaco-editor/esm/vs/editor/editor.api";
import "monaco-editor/esm/vs/basic-languages/sql/sql.contribution";
import { loader } from "@monaco-editor/react";
import EditorWorker from "monaco-editor/esm/vs/editor/editor.worker?worker";

(self as unknown as { MonacoEnvironment: unknown }).MonacoEnvironment = {
  getWorker: () => new EditorWorker(),
};

loader.config({ monaco });

export { monaco };
