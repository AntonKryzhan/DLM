module examples.text_and_io

theory Greeter {
    fn greet(name: Text) -> Text = concat("Hello, ", concat(name, "!"))
    fn shout(t: Text) -> Text = concat(t, "!!") // a comment with "quotes" and // slashes
    fn count_words(t: Text) -> Nat = length(words(t))
    fn parse_or_zero(t: Text) -> Nat = match text_to_nat(t) { some(n) => n, none => 0 }

    theorem greet_length: nat_eq(text_length(greet("DLM")), 11)
    theorem text_refl(t: Text): text_eq(t, t) by normalize
    theorem greet_dlm(n: Nat): text_eq(greet(nat_to_text(n + 0)), greet(nat_to_text(n))) by normalize
    theorem literal_eq: text_eq(concat("a", "b"), "ab")

    let first = read_line()
    let name = match first { some(line) => line, none => "world" }
    let rest = read_all()
    print_line(greet(name))
    print_line(join(lines(rest), " | "))
    print_line(nat_to_text(count_words(rest) + parse_or_zero("42")))
    print_line("tab:\tquote:\" backslash:\\ url: http://example.com")
}
