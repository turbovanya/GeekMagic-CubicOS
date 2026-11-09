"""Console entry point for the standalone Windows sender."""

import re
import sys

from pc_metrics import main


def run() -> None:
    if not sys.stdout.isatty():
        sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
    if len(sys.argv) == 1:
        try:
            while True:
                host = input("Адрес кубика (IP или имя .local, Enter для выхода): ").strip()
                if not host:
                    return
                if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.-]*", host):
                    sys.argv.extend(("--host", host))
                    break
                print("Введите IP или имя кубика без http:// и номера порта.")
        except (EOFError, KeyboardInterrupt):
            return
    main()


if __name__ == "__main__":
    run()
