// Freestanding C compiled on the desktop; only this .wasm is uploaded.
__attribute__((import_module("console"), import_name("write")))
extern void console_write(const char *bytes, unsigned length);

__attribute__((export_name("run")))
int run(void) {
    static const char message[] = "Hello from a compiled C module!\n";
    console_write(message, sizeof(message) - 1);
    return 7;
}
