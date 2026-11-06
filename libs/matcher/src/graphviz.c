#include "graphviz.h"

#include "utf8.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MATCHER_DOT_NONE UINT32_MAX

static bool matcher_dot_valid_literal(
    size_t total_length,
    uint32_t offset,
    uint32_t length
) {
    return (size_t)offset <= total_length &&
           (size_t)length <= total_length - (size_t)offset;
}

static bool matcher_dot_write_literal(
    FILE *out,
    const unsigned char *data,
    size_t length
) {
    matcher_utf8_cursor_t cursor;
    matcher_utf8_grapheme_t grapheme;

    matcher_utf8_cursor_init(&cursor, data, length);

    while (matcher_utf8_next(&cursor, &grapheme)) {
        for (size_t i = 0; i < grapheme.length; ++i) {
            const unsigned char ch = grapheme.data[i];

            if (ch == '"') {
                fputs("\\\"", out);
            } else if (ch == '\\') {
                fputs("\\\\", out);
            } else if (ch < 0x20u ||
                       ch == 0x7Fu ||
                       (ch >= 0x80u && !grapheme.valid_utf8)) {
                /*
                 * Emit a printable byte representation.
                 * Two backslashes preserve the literal "\xHH"
                 * notation inside a DOT label.
                 */
                fprintf(out, "\\\\x%02X", (unsigned)ch);
            } else {
                fputc(ch, out);
            }
        }
    }

    return !ferror(out);
}

static bool matcher_dot_valid_nfa(
    const matcher_nfa_t *nfa
) {
    if (nfa == NULL ||
        nfa->state_count == 0 ||
        nfa->edge_offsets == NULL ||
        nfa->start_state >= nfa->state_count ||
        nfa->accept_state >= nfa->state_count) {
        return false;
    }

    if (nfa->edge_count != 0 && nfa->edges == NULL) {
        return false;
    }

    if (nfa->literals_length != 0 && nfa->literals == NULL) {
        return false;
    }

    if (nfa->edge_offsets[0] != 0 ||
        nfa->edge_offsets[nfa->state_count] != nfa->edge_count) {
        return false;
    }

    for (uint32_t state = 0; state < nfa->state_count; ++state) {
        const uint32_t begin = nfa->edge_offsets[state];
        const uint32_t end = nfa->edge_offsets[state + 1];

        if (begin > end || end > nfa->edge_count) {
            return false;
        }

        for (uint32_t i = begin; i < end; ++i) {
            const matcher_nfa_edge_t *edge = &nfa->edges[i];

            if (edge->to >= nfa->state_count) {
                return false;
            }

            switch (edge->kind) {
                case MATCHER_NFA_LITERAL:
                    if (!matcher_dot_valid_literal(
                            nfa->literals_length,
                            edge->literal_offset,
                            edge->literal_length)) {
                        return false;
                    }
                    break;

                case MATCHER_NFA_ANY_ONE:
                case MATCHER_NFA_ANY_MANY:
                    break;

                default:
                    return false;
            }
        }
    }

    return true;
}

static bool matcher_dot_valid_dfa(
    const matcher_dfa_t *dfa
) {
    if (dfa == NULL ||
        dfa->state_count == 0 ||
        dfa->class_count == 0 ||
        dfa->transitions == NULL ||
        dfa->accepting == NULL ||
        dfa->start_state >= dfa->state_count) {
        return false;
    }

    if (dfa->class_count > 1 && dfa->classes == NULL) {
        return false;
    }

    if (dfa->literals_length != 0 && dfa->literals == NULL) {
        return false;
    }

    if ((size_t)dfa->state_count >
        SIZE_MAX / (size_t)dfa->class_count) {
        return false;
    }

    for (uint32_t symbol_class = 1;
         symbol_class < dfa->class_count;
         ++symbol_class) {
        const matcher_dfa_class_t *entry =
            &dfa->classes[symbol_class];

        if (!matcher_dot_valid_literal(
                dfa->literals_length,
                entry->literal_offset,
                entry->literal_length)) {
            return false;
        }

        if (entry->literal_length == 0) {
            return false;
        }
    }

    const size_t transition_count =
        (size_t)dfa->state_count * dfa->class_count;

    for (size_t i = 0; i < transition_count; ++i) {
        if ((uint32_t)dfa->transitions[i] >= dfa->state_count) {
            return false;
        }
    }

    return true;
}

static bool matcher_dot_write_class(
    FILE *out,
    const matcher_dfa_t *dfa,
    uint32_t symbol_class
) {
    if (symbol_class == 0) {
        fputs("OTHER", out);
        return !ferror(out);
    }

    const matcher_dfa_class_t *entry =
        &dfa->classes[symbol_class];

    fprintf(out, "c%u:", (unsigned)symbol_class);

    return matcher_dot_write_literal(
        out,
        dfa->literals + entry->literal_offset,
        entry->literal_length
    );
}

bool matcher_graphviz_write_nfa(
    const matcher_nfa_t *nfa,
    FILE *out
) {
    if (out == NULL || !matcher_dot_valid_nfa(nfa)) {
        return false;
    }

    fputs("digraph NFA {\n", out);
    fputs("  graph [charset=\"UTF-8\", rankdir=LR];\n", out);
    fputs("  node [shape=circle];\n", out);
    fputs("  start [shape=point, label=\"\"];\n", out);

    fprintf(
        out,
        "  start -> s%u;\n",
        (unsigned)nfa->start_state
    );

    for (uint32_t state = 0; state < nfa->state_count; ++state) {
        fprintf(
            out,
            "  s%u [shape=%s];\n",
            (unsigned)state,
            state == nfa->accept_state
                ? "doublecircle"
                : "circle"
        );
    }

    for (uint32_t state = 0; state < nfa->state_count; ++state) {
        const uint32_t begin = nfa->edge_offsets[state];
        const uint32_t end = nfa->edge_offsets[state + 1];

        for (uint32_t i = begin; i < end; ++i) {
            const matcher_nfa_edge_t *edge = &nfa->edges[i];

            fprintf(
                out,
                "  s%u -> s%u [label=\"",
                (unsigned)state,
                (unsigned)edge->to
            );

            switch (edge->kind) {
                case MATCHER_NFA_LITERAL:
                    fputs("L:", out);

                    matcher_dot_write_literal(
                        out,
                        nfa->literals + edge->literal_offset,
                        edge->literal_length
                    );
                    break;

                case MATCHER_NFA_ANY_ONE:
                    fputc('.', out);
                    break;

                case MATCHER_NFA_ANY_MANY:
                    fputc('*', out);
                    break;

                default:
                    return false;
            }

            fputs("\"];\n", out);
        }
    }

    fputs("}\n", out);

    return !ferror(out);
}

static bool matcher_dot_write_dfa(
    const matcher_dfa_t *dfa,
    FILE *out,
    const char *graph_name
) {
    if (out == NULL || !matcher_dot_valid_dfa(dfa)) {
        return false;
    }

    /*
     * Build one linked list of alphabet classes per target state.
     * This groups all transitions with the same source and target.
     *
     * Additional memory:
     *   O(state_count + class_count)
     */
    uint32_t *head = malloc(
        (size_t)dfa->state_count * sizeof(*head)
    );

    uint32_t *tail = malloc(
        (size_t)dfa->state_count * sizeof(*tail)
    );

    uint32_t *next = malloc(
        (size_t)dfa->class_count * sizeof(*next)
    );

    if (head == NULL || tail == NULL || next == NULL) {
        free(next);
        free(tail);
        free(head);
        return false;
    }

    fprintf(out, "digraph %s {\n", graph_name);
    fputs("  graph [charset=\"UTF-8\", rankdir=LR];\n", out);
    fputs("  node [shape=circle];\n", out);
    fputs("  start [shape=point, label=\"\"];\n", out);

    fprintf(
        out,
        "  start -> s%u;\n",
        (unsigned)dfa->start_state
    );

    for (uint32_t state = 0; state < dfa->state_count; ++state) {
        fprintf(
            out,
            "  s%u [shape=%s];\n",
            (unsigned)state,
            dfa->accepting[state]
                ? "doublecircle"
                : "circle"
        );
    }

    for (uint32_t state = 0; state < dfa->state_count; ++state) {
        for (uint32_t target = 0;
             target < dfa->state_count;
             ++target) {
            head[target] = MATCHER_DOT_NONE;
            tail[target] = MATCHER_DOT_NONE;
        }

        const size_t row_offset =
            (size_t)state * dfa->class_count;

        for (uint32_t symbol_class = 0;
             symbol_class < dfa->class_count;
             ++symbol_class) {
            const uint32_t target =
                dfa->transitions[row_offset + symbol_class];

            next[symbol_class] = MATCHER_DOT_NONE;

            if (head[target] == MATCHER_DOT_NONE) {
                head[target] = symbol_class;
            } else {
                next[tail[target]] = symbol_class;
            }

            tail[target] = symbol_class;
        }

        for (uint32_t target = 0;
             target < dfa->state_count;
             ++target) {
            if (head[target] == MATCHER_DOT_NONE) {
                continue;
            }

            fprintf(
                out,
                "  s%u -> s%u [label=\"",
                (unsigned)state,
                (unsigned)target
            );

            bool first = true;

            for (uint32_t symbol_class = head[target];
                 symbol_class != MATCHER_DOT_NONE;
                 symbol_class = next[symbol_class]) {
                if (!first) {
                    fputs(", ", out);
                }

                matcher_dot_write_class(
                    out,
                    dfa,
                    symbol_class
                );

                first = false;
            }

            fputs("\"];\n", out);
        }
    }

    fputs("}\n", out);

    const bool success = !ferror(out);

    free(next);
    free(tail);
    free(head);

    return success;
}

bool matcher_graphviz_write_dfa(
    const matcher_dfa_t *dfa,
    FILE *out
) {
    return matcher_dot_write_dfa(
        dfa,
        out,
        "DFA"
    );
}

bool matcher_graphviz_write_min_dfa(
    const matcher_dfa_t *dfa,
    FILE *out
) {
    return matcher_dot_write_dfa(
        dfa,
        out,
        "Min_DFA"
    );
}
