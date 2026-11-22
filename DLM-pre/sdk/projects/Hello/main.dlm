module app.hello
import std.collections

theory Hello extends StdCollections {
    let values = list_reverse(list(1, 2, 3))
    print_line("DLM standalone SDK is ready")
    print_decimal(unwrap_or(head(values), 0))
}
