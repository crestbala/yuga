/** Playground web UI. Proxies gRPC-Web `POST /Playground/*` to :8081
 *  and serves the Zeus Canvas2D loader. */
import { createReadStream, existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, "../../../..");
const loader = resolve(repo, "packages/zeus/web/loader.js");

export default defineConfig({
  server: {
    host: "127.0.0.1",
    port: 5174,
    proxy: {
      "/Playground": {
        target: "http://127.0.0.1:8081",
        timeout: 120000,
      },
    },
  },
  plugins: [
    {
      name: "zeus-loader",
      configureServer(server) {
        server.middlewares.use((req, res, next) => {
          const url = (req.url || "").split("?")[0];
          if (url !== "/loader.js") {
            next();
            return;
          }
          if (!existsSync(loader)) {
            res.statusCode = 404;
            res.end("missing loader.js");
            return;
          }
          res.setHeader("Content-Type", "text/javascript; charset=utf-8");
          createReadStream(loader).pipe(res);
        });
      },
    },
  ],
});
