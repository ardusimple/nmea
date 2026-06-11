import js from "@eslint/js";
import ts from "typescript-eslint";

export default [
  {
    ignores: ["node_modules", "dist"],
  },
  js.configs.recommended,
  ...ts.configs.recommended,
  {
    files: ["**/*.ts"],
    languageOptions: {
      parser: ts.parser,
      parserOptions: {
        project: "./tsconfig.json",
      },
    },
    rules: {
      "no-console": "warn",
    },
  },
];
