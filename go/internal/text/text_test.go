package text

import (
	"testing"
)

func TestNormalizeText(t *testing.T) {
	tests := []struct {
		input, want string
	}{
		{"hello  world", "hello world"},
		{"line1\n\n\nline2", "line1\nline2"},
		{"  spaces  ", "spaces"},
		{"tabs\t\there", "tabs here"},
		{"", ""},
	}

	for _, tc := range tests {
		got := NormalizeText(tc.input)
		if got != tc.want {
			t.Errorf("NormalizeText(%q) = %q, want %q", tc.input, got, tc.want)
		}
	}
}

func TestStartsWithBullet(t *testing.T) {
	tests := []struct {
		input string
		want  bool
	}{
		{"• item", true},
		{"- item", true},
		{"1. item", true},
		{"1) item", true},
		{"regular text", false},
		{"", false},
		{"  • indented bullet", true},
		{"10. numbered", true},
	}

	for _, tc := range tests {
		got := StartsWithBullet(tc.input)
		if got != tc.want {
			t.Errorf("StartsWithBullet(%q) = %v, want %v", tc.input, got, tc.want)
		}
	}
}

func TestStartsWithNumber(t *testing.T) {
	tests := []struct {
		input  string
		isNum  bool
		prefix string
	}{
		{"1. text", true, "1."},
		{"10) text", true, "10)"},
		{"a. text", true, "a."},
		{"• bullet", false, ""},
		{"no number", false, ""},
	}

	for _, tc := range tests {
		isNum, prefix := StartsWithNumber(tc.input)
		if isNum != tc.isNum || prefix != tc.prefix {
			t.Errorf("StartsWithNumber(%q) = (%v, %q), want (%v, %q)", tc.input, isNum, prefix, tc.isNum, tc.prefix)
		}
	}
}

func TestStartsWithNumericHeading(t *testing.T) {
	tests := []struct {
		input string
		want  bool
	}{
		{"1. Introduction", true},
		{"1.2.3 Subsection", true},
		{"Section 1", false},
		{"A) Item", false},
	}

	for _, tc := range tests {
		got := StartsWithNumericHeading(tc.input)
		if got != tc.want {
			t.Errorf("StartsWithNumericHeading(%q) = %v, want %v", tc.input, got, tc.want)
		}
	}
}

func TestStartsWithHeadingKeyword(t *testing.T) {
	tests := []struct {
		input string
		want  bool
	}{
		{"Chapter 1", true},
		{"CHAPTER 2", true},
		{"Section A", true},
		{"Appendix B", true},
		{"Introduction", false},
		{"", false},
	}

	for _, tc := range tests {
		got := StartsWithHeadingKeyword(tc.input)
		if got != tc.want {
			t.Errorf("StartsWithHeadingKeyword(%q) = %v, want %v", tc.input, got, tc.want)
		}
	}
}

func TestIsAllCaps(t *testing.T) {
	tests := []struct {
		input string
		want  bool
	}{
		{"HELLO WORLD", true},
		{"HELLO 123", true},
		{"Hello World", false},
		{"123", false},
		{"", false},
	}

	for _, tc := range tests {
		got := IsAllCaps(tc.input)
		if got != tc.want {
			t.Errorf("IsAllCaps(%q) = %v, want %v", tc.input, got, tc.want)
		}
	}
}

func TestHasVisibleContent(t *testing.T) {
	tests := []struct {
		input string
		want  bool
	}{
		{"hello", true},
		{"   ", false},
		{"\n\t", false},
		{"a", true},
		{"", false},
	}

	for _, tc := range tests {
		got := HasVisibleContent(tc.input)
		if got != tc.want {
			t.Errorf("HasVisibleContent(%q) = %v, want %v", tc.input, got, tc.want)
		}
	}
}

func TestEndsWithPunctuation(t *testing.T) {
	tests := []struct {
		input string
		want  bool
	}{
		{"hello.", true},
		{"hello?", true},
		{"hello!", true},
		{"hello:", true},
		{"hello", false},
		{"", false},
	}

	for _, tc := range tests {
		got := EndsWithPunctuation(tc.input)
		if got != tc.want {
			t.Errorf("EndsWithPunctuation(%q) = %v, want %v", tc.input, got, tc.want)
		}
	}
}

func TestIsLonePageNumber(t *testing.T) {
	tests := []struct {
		input string
		want  bool
	}{
		{"1", true},
		{"42", true},
		{"1234", true},
		{"12345", false},
		{"page 1", false},
		{"", false},
	}

	for _, tc := range tests {
		got := IsLonePageNumber(tc.input)
		if got != tc.want {
			t.Errorf("IsLonePageNumber(%q) = %v, want %v", tc.input, got, tc.want)
		}
	}
}

func TestIsBullet(t *testing.T) {
	bullets := []rune{'•', '●', '○', '▪', '■', '-', '*', '+'}
	for _, r := range bullets {
		if !IsBullet(r) {
			t.Errorf("IsBullet(%q) = false, want true", r)
		}
	}

	nonBullets := []rune{'a', '1', ' ', '.'}
	for _, r := range nonBullets {
		if IsBullet(r) {
			t.Errorf("IsBullet(%q) = true, want false", r)
		}
	}
}

func TestIsInMarginArea(t *testing.T) {
	pageBBox := [4]float32{0, 0, 612, 792}

	topMargin := [4]float32{100, 10, 200, 30}
	if !IsInMarginArea(topMargin, pageBBox, 0.08) {
		t.Error("expected top margin to be detected")
	}

	bottomMargin := [4]float32{100, 760, 200, 780}
	if !IsInMarginArea(bottomMargin, pageBBox, 0.08) {
		t.Error("expected bottom margin to be detected")
	}

	middle := [4]float32{100, 300, 200, 400}
	if IsInMarginArea(middle, pageBBox, 0.08) {
		t.Error("middle content should not be in margin")
	}
}
