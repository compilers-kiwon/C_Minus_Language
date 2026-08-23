# C− 문법 분석 노트

`c-minus.md`의 BNF를 컴파일러 구현 관점에서 분해한 결과.

---

## 1. 토큰 집합 (28 + 2)

| 분류 | 토큰 |
| :- | :- |
| 키워드 (6) | `else` `if` `int` `return` `void` `while` |
| 연산자 (11) | `+` `-` `*` `/` `<` `<=` `>` `>=` `==` `!=` `=` |
| 구분자 (9) | `;` `,` `(` `)` `[` `]` `{` `}` |
| 리터럴/이름 | `ID` `NUM` |
| 그 외 | `/* */` (주석, 토큰 아님) · `EOF` · `ERROR` |

### 스캐너 DFA에서 주의할 점
- **Maximal munch**: `<` vs `<=`, `>` vs `>=`, `=` vs `==`.
- `!`는 **단독으로 존재하지 않는다.** `!` 다음이 `=`가 아니면 즉시 어휘 오류.
- `/`는 나눗셈이거나 주석 시작(`/*`). 한 글자 lookahead 필요.
- 주석은 중첩 불가 → 상태 두 개(`INCOMMENT`, `INCOMMENT_STAR`)로 충분.
  주석 안에서 EOF를 만나면 "unterminated comment" 오류.
- `ID`는 letter만 → `abc1`은 `ID(abc)` + `NUM(1)` 두 토큰으로 쪼개진다 (오류 아님).
  이것이 C와 다른 대표적 함정.
- `NUM`은 부호 없음. **단항 마이너스가 문법에 없으므로** `-5`는 표현식으로 쓸 수 없다.

---

## 2. FIRST / FOLLOW

`$`는 EOF, `relop` = {`<=` `<` `>` `>=` `==` `!=`}, `addop` = {`+` `-`}, `mulop` = {`*` `/`}.

| 비단말 | FIRST | FOLLOW |
| :- | :- | :- |
| program | int void | $ |
| declaration-list | int void | $ int void |
| declaration | int void | $ int void |
| var-declaration | int void | $ int void, FIRST(statement), `}` |
| type-specifier | int void | ID |
| fun-declaration | int void | $ int void |
| params | int void | `)` |
| param-list | int void | `)` `,` |
| param | int void | `)` `,` |
| compound-stmt | `{` | $ int void, FIRST(statement), `}` `else` |
| local-declarations | int void ε | FIRST(statement), `}` |
| statement-list | FIRST(statement) ε | `}` |
| statement | ID NUM `(` `;` `{` if while return | FIRST(statement), `}` `else` |
| expression-stmt | ID NUM `(` `;` | = FOLLOW(statement) |
| selection-stmt | if | = FOLLOW(statement) |
| iteration-stmt | while | = FOLLOW(statement) |
| return-stmt | return | = FOLLOW(statement) |
| expression | ID NUM `(` | `;` `)` `]` `,` |
| var | ID | `=` `;` `)` `]` `,` relop addop mulop |
| simple-expression | ID NUM `(` | `;` `)` `]` `,` |
| additive-expression | ID NUM `(` | `;` `)` `]` `,` relop |
| term | ID NUM `(` | 위 + addop |
| factor | ID NUM `(` | 위 + mulop |
| call | ID | = FOLLOW(factor) |
| args | ID NUM `(` ε | `)` |
| arg-list | ID NUM `(` | `)` `,` |

**핵심 결과: `=` ∉ FOLLOW(factor).**
따라서 `ID`를 `var`로 환원한 뒤 lookahead가 `=`이면 규칙 18(대입), 아니면 `factor → var`.
LR(1)에서 충돌 없이 결정된다.

---

## 3. 모호성 / 충돌

| 위치 | 성격 | 해결 |
| :- | :- | :- |
| 규칙 15 dangling else | **진짜 모호성** | shift 우선 = 가장 가까운 `if`에 결합. bison은 기본 shift(경고 1건)이므로 `%prec`/`%nonassoc` 또는 그냥 수용 |
| 규칙 7 `params → param-list \| void` | LL(1) 충돌 (LR은 무충돌) | `void` 다음이 `)`면 무인자, `ID`면 void 타입 파라미터 |
| 규칙 18/26 `var = expr` vs `factor → var` | 없음 (위 FOLLOW 결과) | — |
| 규칙 19/27 `ID [` vs `ID (` vs `ID` | 없음 | ID 다음 1토큰으로 분기 |
| 규칙 20 relop 비결합 | 문법 자체가 비결합 | `a < b < c`는 **구문 오류**. bison이면 `%nonassoc` 불필요 (BNF가 이미 막음) |

**결론: dangling else를 제외하면 LALR(1) 무충돌.** bison에 그대로 넣어도 된다.

---

## 4. LL(1) / 재귀 하강용 변형

좌재귀 규칙: **2, 8, 11, 12, 22, 24, 29**.

### 4-1. 선언부 — 좌인수분해가 잘 맞는다
`var-declaration`과 `fun-declaration`은 `type-specifier ID` 접두를 공유한다.

```
declaration        -> type-specifier ID declaration'
declaration'       -> ;                              /* 단순 변수  */
                    | [ NUM ] ;                      /* 배열       */
                    | ( params ) compound-stmt       /* 함수       */
```

### 4-2. params
```
params      -> void params'
             | int ID param'  param-list'
params'     -> ID param' param-list'     /* void 파라미터 → 의미 오류로 처리 */
             | ε                         /* 인자 없음 */
param       -> type-specifier ID param'
param'      -> [ ] | ε
param-list' -> , param param-list' | ε
```

### 4-3. 블록/문장
```
local-declarations -> var-declaration local-declarations | ε   /* FIRST={int,void} */
statement-list     -> statement statement-list | ε             /* ε ⟸ lookahead '}' */
selection-stmt     -> if ( expression ) statement else-part
else-part          -> else statement | ε      /* 탐욕적 매칭 = dangling else 표준 해석 */
return-stmt        -> return return-value ;
return-value       -> expression | ε
```
선언과 문장의 경계는 깔끔하다 — 어떤 문장도 `int`/`void`로 시작하지 않는다.

### 4-4. 식 — 유일하게 까다로운 부분
`expression → var = expression`은 `var`가 `ID [ expression ]`일 수 있어서
접두 길이가 무한하다. 순수 LL(1) 테이블로는 못 푼다. 실용적 해법 두 가지:

1. **파싱 후 판정 (권장)** — `simple-expression`을 먼저 파싱하고, 다음 토큰이 `=`면
   방금 만든 노드가 `var`(ID 또는 첨자식)인지 검사해 대입 노드로 재조립.
   `var`가 아니면 "invalid l-value" 오류.
2. **2토큰 lookahead** — `ID` 다음이 `=`면 대입, `[`면 첨자 파싱 후 다시 `=` 확인.

좌재귀 제거된 산술식:
```
simple-expression   -> additive-expression [ relop additive-expression ]   /* 0 or 1회 */
additive-expression -> term { addop term }        /* 좌결합 루프 */
term                -> factor { mulop factor }    /* 좌결합 루프 */
factor              -> ( expression ) | NUM | ID factor'
factor'             -> ( args ) | [ expression ] | ε
```
`{...}`는 while 루프로 구현하고 **왼쪽으로 접어서(left-fold)** AST를 만들어야
좌결합이 보존된다.

---

## 5. AST 노드 설계 (제안)

```
Decl   := VarDecl(type, name, arraySize?)      /* arraySize 없으면 스칼라 */
        | ParamDecl(type, name, isArray)
        | FunDecl(retType, name, params[], body:Compound)
Stmt   := Compound(localDecls[], stmts[])
        | ExprStmt(expr?)                      /* expr 없으면 빈 문장 ';' */
        | If(cond, then, else?)
        | While(cond, body)
        | Return(expr?)
Expr   := Assign(var:Var, value:Expr)
        | Binary(op, lhs, rhs)                 /* relop/addop/mulop 통합 */
        | Var(name, index?)                    /* index 없으면 스칼라 참조 */
        | Call(name, args[])
        | Num(value)
```
각 노드에 `line`, `col`, 그리고 의미 분석 후 `type`(Int / Void / IntArray)과
심볼 테이블 링크를 붙인다.

---

## 6. 의미 분석 체크리스트

스코프는 **전역 → 함수(파라미터) → 복합문(중첩)** 의 스택 구조.
`input`/`output`은 전역 스코프에 사전 삽입한다.

| # | 규칙 | 오류 메시지 예 |
| :- | :- | :- |
| S1 | `void` 타입 변수/파라미터 금지 | `variable 'x' declared void` |
| S2 | 배열 크기는 NUM (0 이하 여부는 정책 선택) | `invalid array size` |
| S3 | 같은 스코프 중복 선언 금지 | `redeclaration of 'x'` |
| S4 | 사용 전 선언 필수 (전방 참조 없음) | `'f' undeclared` |
| S5 | **마지막 선언은 `void main(void)`** | `'main' must be the last declaration` |
| S6 | 함수를 변수로, 변수를 함수로 쓰지 않기 | `'x' is not a function` |
| S7 | 호출 인자 **개수** = 파라미터 개수 | `too few arguments to 'f'` |
| S8 | 인자 **종류** 일치: 배열 파라미터 ⟸ 배열 이름, 스칼라 파라미터 ⟸ int 식 | `array expected` |
| S9 | 배열 변수는 첨자 필수 (S8의 예외만 허용) | `array 'a' used without subscript` |
| S10 | 첨자식은 int (배열/void 불가) | `array subscript is not an integer` |
| S11 | 산술/관계 피연산자는 int (void 호출 결과 금지) | `void value not ignored` |
| S12 | 대입 좌변은 var, 우변 타입은 int | `invalid assignment target` |
| S13 | 비-void 함수: `return expr;` / void 함수: `return;` | `return with no value in non-void function` |
| S14 | 조건식(if/while)은 int | `void used in condition` |

S5는 C−만의 특이 규칙이니 잊기 쉽다.

---

## 7. 런타임 / 코드 생성 메모

- 활성 레코드: 파라미터(값 or 배열 베이스 주소), 지역 변수, 반환 주소, 이전 프레임 포인터.
  재귀를 지원해야 하므로 스택 프레임 필수.
- **배열 파라미터는 참조 전달** — 스택에 베이스 주소(포인터)만 올린다.
  전역 배열이든 다른 함수의 배열 파라미터든 그대로 전달 가능.
- 지역 배열은 없다 (지역 선언에도 `int a[10];`이 문법상 가능하지만
  Louden 원 구현은 전역 배열만 다루는 경우가 많다 — 정책 결정 필요).
- `/`는 0으로 나누기 시 런타임 오류.
- **첨자 하한만 검사** (음수 → 프로그램 중단). 상한은 검사하지 않는다.
- 부울 값은 정수 0/1.
- 단축 평가(short-circuit)를 고려할 논리 연산자가 없어 코드 생성이 단순하다.

---

## 8. C와 달라서 걸려 넘어지기 쉬운 것들

1. 단항 `-`, `+` 없음 → `x = -1;` **구문 오류**
2. `!`, `&&`, `||`, `%`, `++`, `--`, 복합 대입(`+=`) 전부 없음
3. `for`, `do-while`, `switch`, `break`, `continue`, `goto` 없음
4. `int a, b;` 불가 — 선언 하나에 변수 하나
5. 식별자에 숫자·밑줄 불가 (`i1`, `my_var` 불가)
6. 문자열·문자 리터럴 없음
7. 함수 프로토타입 없음 → 상호 재귀 불가능 (사용 전 선언 규칙 때문)
8. `main`이 반드시 **마지막** 선언, 시그니처도 `void main(void)` 고정
9. 관계 연산자 연쇄 불가 (`0 < x < 10` 구문 오류)
10. 블록 안 선언은 반드시 문장보다 **앞**
