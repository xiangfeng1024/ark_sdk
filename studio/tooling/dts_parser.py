# /**
#  * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
#  * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
#  *
#  * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
#  *
#  * This source code, together with its associated documentation, examples,
#  * configuration files, and related materials, is collectively referred to
#  * as the "Software".
#  *
#  * Subject to the complete terms set forth in the LICENSE file, Ark Crew
#  * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
#  * right to access, reproduce, and modify the Software solely for personal
#  * study, classroom education, academic research, and non-commercial evaluation.
#  *
#  * Commercial use of the Software, in whole or in part, is strictly prohibited
#  * without prior written authorization from Ark Crew. Prohibited activities
#  * include, without limitation, sale, sublicensing, paid distribution, use in
#  * paid consulting or training, incorporation into any commercial product or
#  * service, and internal development intended for commercial deployment.
#  *
#  * Except for the limited rights expressly granted under the applicable
#  * License, no license or other right, whether express, implied, by estoppel,
#  * or otherwise, is granted under any copyright, patent, trademark, trade
#  * secret, mask work, or other intellectual property right belonging to
#  * Ark Crew or any third party.
#  *
#  * Delivery or disclosure of the Software does not convey permission to use
#  * the Ark Crew name, trademarks, logos, visual identity, or other branding,
#  * except where strictly necessary to preserve the original attribution.
#  *
#  * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
#  * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
#  * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
#  * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
#  * NON-INFRINGEMENT.
#  *
#  * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
#  * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
#  * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
#  * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
#  *
#  * This notice shall be retained in all authorized copies or substantial
#  * portions of the Software. Removal, concealment, or unauthorized alteration
#  * of this notice is prohibited.
#  *
#  * See the LICENSE file in the root directory of this repository for the
#  * complete and controlling license terms.
#  */

"""Small dependency-free parser for the Linux DTS v1 syntax used by ARK CREW SDK."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import re
from typing import Iterator


@dataclass
class DtsReference:
    label: str


@dataclass
class DtsProperty:
    name: str
    kind: str
    value: object = None
    line: int = 0


@dataclass
class DtsNode:
    name: str
    label: str | None = None
    parent: "DtsNode | None" = None
    properties: dict[str, DtsProperty] = field(default_factory=dict)
    children: list["DtsNode"] = field(default_factory=list)
    phandle: int = 0

    @property
    def path(self) -> str:
        if self.parent is None:
            return "/"
        base = self.parent.path.rstrip("/")
        return f"{base}/{self.name}"

    def child(self, name: str) -> "DtsNode | None":
        return next((item for item in self.children if item.name == name), None)

    def prop(self, name: str) -> DtsProperty | None:
        return self.properties.get(name)

    def strings(self, name: str) -> tuple[str, ...]:
        prop = self.prop(name)
        if prop is None or prop.kind != "strings":
            return ()
        return tuple(prop.value)

    def cells(self, name: str) -> tuple[object, ...]:
        prop = self.prop(name)
        if prop is None or prop.kind != "cells":
            return ()
        return tuple(prop.value)

    def boolean(self, name: str) -> bool:
        prop = self.prop(name)
        return prop is not None and prop.kind == "bool"


@dataclass
class DtsDocument:
    root: DtsNode
    labels: dict[str, DtsNode]

    def walk(self, node: DtsNode | None = None) -> Iterator[DtsNode]:
        current = self.root if node is None else node
        yield current
        for child in current.children:
            yield from self.walk(child)

    def find_path(self, path: str) -> DtsNode | None:
        return next((node for node in self.walk() if node.path == path), None)


@dataclass(frozen=True)
class _Token:
    kind: str
    text: str
    line: int
    column: int


_TOKEN_RE = re.compile(
    r"(?P<SPACE>[ \t\r\n]+)|"
    r"(?P<COMMENT>//[^\n]*|/\*.*?\*/)|"
    r'(?P<STRING>"(?:\\.|[^"\\])*")|'
    r"(?P<NUMBER>-?(?:0[xX][0-9A-Fa-f]+|[0-9][0-9A-Fa-f]*))|"
    r"(?P<DIRECTIVE>/dts-v1/|/include/|/delete-node/|/delete-property/)|"
    r"(?P<IDENT>[A-Za-z_#][A-Za-z0-9,._+?#-]*)|"
    r"(?P<SYMBOL>[{};=<>\[\],:@&/])",
    re.DOTALL,
)


def _decode_string(text: str) -> str:
    return bytes(text[1:-1], "utf-8").decode("unicode_escape")


def _expand_includes(path: Path, sdk_root: Path, stack: tuple[Path, ...] = ()) -> str:
    path = path.resolve()
    allowed = (sdk_root.resolve(), path.parent.resolve())
    if path in stack:
        raise ValueError(f"recursive DTS include: {path}")
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise ValueError(f"DTS does not exist: {path}") from exc
    pattern = re.compile(r'/include/\s*"([^"]+)"\s*;?')

    def replace(match: re.Match[str]) -> str:
        included = (path.parent / match.group(1)).resolve()
        if not any(included == root or root in included.parents for root in allowed):
            raise ValueError(f"DTS include escapes SDK/App directory: {included}")
        return _expand_includes(included, sdk_root, stack + (path,))

    return pattern.sub(replace, text)


class _Parser:
    def __init__(self, text: str) -> None:
        self.tokens = self._tokenize(text)
        self.index = 0
        self.labels: dict[str, DtsNode] = {}
        self.overlays: list[tuple[str, DtsNode]] = []

    @staticmethod
    def _tokenize(text: str) -> list[_Token]:
        tokens: list[_Token] = []
        position = 0
        line = 1
        column = 1
        while position < len(text):
            match = _TOKEN_RE.match(text, position)
            if match is None:
                raise ValueError(f"invalid DTS token at {line}:{column}: {text[position:position + 20]!r}")
            raw = match.group(0)
            kind = match.lastgroup or ""
            if kind not in {"SPACE", "COMMENT"}:
                tokens.append(_Token(kind, raw, line, column))
            newlines = raw.count("\n")
            if newlines:
                line += newlines
                column = len(raw.rsplit("\n", 1)[-1]) + 1
            else:
                column += len(raw)
            position = match.end()
        tokens.append(_Token("EOF", "", line, column))
        return tokens

    def peek(self, text: str | None = None, kind: str | None = None) -> bool:
        token = self.tokens[self.index]
        return (text is None or token.text == text) and (kind is None or token.kind == kind)

    def take(self, text: str | None = None, kind: str | None = None) -> _Token:
        token = self.tokens[self.index]
        if (text is not None and token.text != text) or (kind is not None and token.kind != kind):
            expected = text or kind
            raise ValueError(f"expected {expected} at {token.line}:{token.column}, got {token.text!r}")
        self.index += 1
        return token

    def parse(self) -> DtsDocument:
        self.take("/dts-v1/")
        self.take(";")
        root = self._parse_node(None, root=True)
        while not self.peek(kind="EOF"):
            if self.peek("/"):
                self._merge(root, self._parse_node(None, root=True))
            else:
                self.take("&")
                label = self.take(kind="IDENT").text
                overlay = self._parse_node(None, forced_name=f"&{label}")
                self.overlays.append((label, overlay))
        for label, overlay in self.overlays:
            target = self.labels.get(label)
            if target is None:
                raise ValueError(f"unresolved DTS overlay label: {label}")
            self._merge(target, overlay)
        phandle = 1
        for node in self._walk(root):
            if node.label:
                node.phandle = phandle
                phandle += 1
        for node in self._walk(root):
            for prop in node.properties.values():
                if prop.kind == "cells":
                    for item in prop.value:
                        if isinstance(item, DtsReference) and item.label not in self.labels:
                            raise ValueError(f"unresolved phandle '&{item.label}' at {prop.line}")
        return DtsDocument(root, self.labels)

    def _parse_node(
        self,
        parent: DtsNode | None,
        root: bool = False,
        forced_name: str | None = None,
    ) -> DtsNode:
        label: str | None = None
        if root:
            self.take("/")
            name = ""
        elif forced_name is not None:
            name = forced_name
        else:
            first = self.take(kind="IDENT")
            if self.peek(":"):
                self.take(":")
                label = first.text
                first = self.take(kind="IDENT")
            name = first.text
            if self.peek("@"):
                self.take("@")
                address = self.take()
                if address.kind not in {"NUMBER", "IDENT"}:
                    raise ValueError(f"invalid unit address at {address.line}:{address.column}")
                name += "@" + address.text
        node = DtsNode(name=name, label=label, parent=parent)
        if label:
            if label in self.labels:
                raise ValueError(f"duplicate DTS label: {label}")
            self.labels[label] = node
        self.take("{")
        while not self.peek("}"):
            if self.peek(kind="EOF"):
                token = self.tokens[self.index]
                raise ValueError(f"unterminated node at {token.line}:{token.column}")
            first = self.take(kind="IDENT")
            item_label: str | None = None
            if self.peek(":"):
                self.take(":")
                item_label = first.text
                first = self.take(kind="IDENT")
            if self.peek("{") or self.peek("@"):
                self.index -= 1
                if item_label is not None:
                    self.index -= 2
                child = self._parse_node(node)
                node.children.append(child)
                continue
            prop = self._parse_property(first, item_label)
            if prop.name in node.properties:
                raise ValueError(f"duplicate property '{prop.name}' in {node.path}")
            node.properties[prop.name] = prop
        self.take("}")
        self.take(";")
        return node

    def _parse_property(self, name_token: _Token, label: str | None) -> DtsProperty:
        del label
        if self.peek(";"):
            self.take(";")
            return DtsProperty(name_token.text, "bool", True, name_token.line)
        self.take("=")
        strings: list[str] = []
        cells: list[object] = []
        bytes_value: list[int] = []
        kind: str | None = None
        while True:
            if self.peek(kind="STRING"):
                if kind not in {None, "strings"}:
                    raise ValueError(f"mixed DTS property types at line {name_token.line}")
                kind = "strings"
                strings.append(_decode_string(self.take().text))
            elif self.peek("<"):
                if kind not in {None, "cells"}:
                    raise ValueError(f"mixed DTS property types at line {name_token.line}")
                kind = "cells"
                self.take("<")
                while not self.peek(">"):
                    if self.peek("&"):
                        self.take("&")
                        cells.append(DtsReference(self.take(kind="IDENT").text))
                    else:
                        token = self.take()
                        if token.kind != "NUMBER":
                            raise ValueError(f"expected DTS cell at {token.line}:{token.column}")
                        cells.append(int(token.text, 0) & 0xFFFFFFFF)
                self.take(">")
            elif self.peek("["):
                if kind not in {None, "bytes"}:
                    raise ValueError(f"mixed DTS property types at line {name_token.line}")
                kind = "bytes"
                self.take("[")
                while not self.peek("]"):
                    token = self.take()
                    if token.kind not in {"NUMBER", "IDENT"} or not re.fullmatch(r"[0-9A-Fa-f]{2}", token.text):
                        raise ValueError(f"expected two-digit DTS byte at {token.line}:{token.column}")
                    bytes_value.append(int(token.text, 16))
                self.take("]")
            else:
                token = self.tokens[self.index]
                raise ValueError(f"invalid property value at {token.line}:{token.column}")
            if not self.peek(","):
                break
            self.take(",")
        self.take(";")
        value = strings if kind == "strings" else cells if kind == "cells" else bytes(bytes_value)
        return DtsProperty(name_token.text, kind or "bool", value, name_token.line)

    def _merge(self, target: DtsNode, overlay: DtsNode) -> None:
        target.properties.update(overlay.properties)
        for incoming in overlay.children:
            existing = target.child(incoming.name)
            if existing is None:
                incoming.parent = target
                target.children.append(incoming)
            else:
                self._merge(existing, incoming)

    def _walk(self, node: DtsNode) -> Iterator[DtsNode]:
        yield node
        for child in node.children:
            yield from self._walk(child)


def parse_dts(path: Path, sdk_root: Path) -> DtsDocument:
    """Parse one DTS document without producing a DTB."""
    return _Parser(_expand_includes(path, sdk_root)).parse()
