from pymupdf4llm_c import to_json
from sys import argv


def main(args: list[str]) -> None:
    input_file = args[1]
    output_file = args[2]
    page_number = int(args[3])
    result = to_json(input_file, output=output_file)
    for i, page in enumerate(result):
        if i == page_number:
            print(page.markdown)



if __name__ == "__main__":
    main(argv)





