// RUN: printf 'Content-Length: 80\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}' | %asc lsp 2>/dev/null | grep -q "definitionProvider"
// Test: LSP server advertises definition capability.

function main(): i32 { return 0; }
