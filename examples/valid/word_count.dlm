module examples.word_count

// A small command-line program: counts lines, words and characters of stdin,
// greets the first program argument and returns 0 as exit status.
theory WordCount {
    fn count_lines(t: Text) -> Nat = length(lines(t))
    fn count_words(t: Text) -> Nat = length(words(t))

    fn report(label: Text, n: Nat) -> Text = concat(label, concat(": ", nat_to_text(n)))

    fn first_arg() -> Text =
        match args() {
            [] => "nobody",
            cons(a, _) => a
        }

    fn main() -> Nat =
        let input = read_all() in
        let _ = print_line(concat("hello, ", first_arg())) in
        let _ = print_line(report("lines", count_lines(input))) in
        let _ = print_line(report("words", count_words(input))) in
        let _ = print_line(report("chars", text_length(input))) in
        0
}
