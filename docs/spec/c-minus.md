# C-Minus (C−) 언어 명세

출처: K. Louden, *Compiler Construction: Principles and Practice*, Appendix A (pp. 491–497).

C−는 C의 부분집합으로, 함수와 1차원 정수 배열을 지원한다. 타입은 `int`와 `void`뿐이며,
포인터·구조체·문자열·부동소수점·전처리기·분리 컴파일은 없다.

---

## 1. 어휘 규약 (Lexical Conventions)

### 1.1 키워드
```
else  if  int  return  void  while
```
모두 예약어이며 소문자만 허용한다.

### 1.2 특수 기호
```
+  -  *  /  <  <=  >  >=  ==  !=  =  ;  ,  (  )  [  ]  {  }  /*  */
```

### 1.3 토큰 ID, NUM
```
ID     = letter letter*
NUM    = digit digit*
letter = a|..|z|A|..|Z
digit  = 0|..|9
```
- 식별자는 **문자만** 허용한다 (숫자, 밑줄 불가).
- 대소문자를 구별한다.

### 1.4 공백
공백(blank), 개행, 탭. 토큰을 구분하는 용도 외에는 무시한다.

### 1.5 주석
`/* ... */`. 공백이 올 수 있는 곳이면 어디든 가능하고(토큰 내부는 불가),
여러 줄에 걸칠 수 있으며, **중첩 불가**.

---

## 2. 문법 (BNF)

```
 1. program             -> declaration-list
 2. declaration-list    -> declaration-list declaration | declaration
 3. declaration         -> var-declaration | fun-declaration
 4. var-declaration     -> type-specifier ID ; | type-specifier ID [ NUM ] ;
 5. type-specifier      -> int | void
 6. fun-declaration     -> type-specifier ID ( params ) compound-stmt
 7. params              -> param-list | void
 8. param-list          -> param-list , param | param
 9. param               -> type-specifier ID | type-specifier ID [ ]
10. compound-stmt       -> { local-declarations statement-list }
11. local-declarations  -> local-declarations var-declaration | empty
12. statement-list      -> statement-list statement | empty
13. statement           -> expression-stmt | compound-stmt | selection-stmt
                         | iteration-stmt | return-stmt
14. expression-stmt     -> expression ; | ;
15. selection-stmt      -> if ( expression ) statement
                         | if ( expression ) statement else statement
16. iteration-stmt      -> while ( expression ) statement
17. return-stmt         -> return ; | return expression ;
18. expression          -> var = expression | simple-expression
19. var                 -> ID | ID [ expression ]
20. simple-expression   -> additive-expression relop additive-expression
                         | additive-expression
21. relop               -> <= | < | > | >= | == | !=
22. additive-expression -> additive-expression addop term | term
23. addop               -> + | -
24. term                -> term mulop factor | factor
25. mulop               -> * | /
26. factor              -> ( expression ) | var | call | NUM
27. call                -> ID ( args )
28. args                -> arg-list | empty
29. arg-list            -> arg-list , expression | expression
```
`empty`는 빈 문자열(ε)이다.

---

## 3. 의미 규칙 (Semantics)

### 3.1 프로그램 (규칙 1–3)
- 프로그램은 전역 변수/함수 선언의 나열이며, 순서는 자유롭다. 선언이 최소 하나는 있어야 한다.
- 모든 변수와 함수는 **사용 전에 선언**되어야 한다 (backpatching 불필요).
- **마지막 선언은 반드시 `void main(void)` 함수**여야 한다.
- 프로토타입이 없으므로 선언과 정의를 구별하지 않는다.

### 3.2 변수 선언 (규칙 4–5)
- 단순 정수 변수 또는 정수 배열. 배열의 첨자 범위는 `0 .. NUM-1`.
- 변수의 타입은 `int`만 유효하다. `void`는 문법상 통과하지만 의미 검사에서 오류로 처리해야 한다.
- 한 선언에 변수 하나만 가능하다 (`int a, b;` 불가).

### 3.3 함수 선언 (규칙 6–9)
- 반환 타입 + 식별자 + 파라미터 목록 + 복합문.
- `void` 반환이면 값을 반환하지 않는다 (procedure).
- 파라미터는 `void`(없음) 또는 콤마로 구분된 목록. `ID [ ]`는 크기 미지정 배열 파라미터.
- 정수 파라미터는 **값 전달**, 배열 파라미터는 **참조 전달**(포인터처럼).
  호출 시 배열 파라미터에는 배열 변수를 넘겨야 한다.
- 함수 타입의 파라미터는 없다.
- 파라미터의 스코프는 그 함수의 복합문이며, 호출마다 별도의 파라미터 집합을 가진다. 재귀 허용.

### 3.4 복합문 (규칙 10–12)
- `{ 지역선언들 문장들 }`. 선언은 문장보다 앞에 온다.
- 지역 변수의 스코프는 해당 복합문이며, 같은 이름의 전역 변수를 가린다.
- 선언 목록과 문장 목록 모두 비어 있을 수 있다 (`{ }` 허용).
- 문장은 순서대로 실행된다.

### 3.5 문장 (규칙 13–17)
- **expression-stmt**: 선택적 식 + `;`. 부수 효과(대입, 호출) 목적으로 사용.
- **if**: 식이 0이 아니면 첫 문장, 0이면(있다면) else 문장 실행.
  **Dangling else**는 가장 가까운 `if`에 붙이는 표준 방식으로 해결한다.
- **while**: 유일한 반복문. 식이 0이 아닌 동안 문장을 반복.
- **return**: 값 유무 선택. `void`가 아닌 함수는 값을 반환해야 하고,
  `void` 함수는 값을 반환하면 안 된다. `main` 안의 `return`은 프로그램 종료.

### 3.6 식 (규칙 18–19)
- `var = expression` 또는 단순식. 대입은 우결합이며 대입된 값이 식 전체의 값이다.
- 대입의 좌변(l-value)이 될 수 있는 것은 **var뿐**이다 (C와 달리 문법으로 제한; 포인터 연산 없음).
- var는 단순 변수 또는 `ID [ expression ]` 형태의 배열 원소.
- 음수 첨자는 **런타임 오류로 프로그램 중단**. 상한은 검사하지 않는다.

### 3.7 단순식 (규칙 20–21)
- 관계 연산자는 **비결합** — 괄호 없이 두 개 이상 쓸 수 없다.
- 관계 연산자가 없으면 가산식의 값, 있으면 참이면 1, 거짓이면 0.

### 3.8 산술식 (규칙 22–25)
- 일반적인 우선순위(`* /` > `+ -`)와 좌결합.
- `/`는 **정수 나눗셈**(나머지 버림).

### 3.9 인자 (규칙 26–29)
- factor: 괄호식, 변수, 함수 호출, NUM.
- 배열 변수는 첨자를 붙여 사용해야 하며, 예외는 **배열 파라미터에 배열 이름 하나만 넘기는 경우**뿐이다.
- 호출 시 인자 개수는 선언된 파라미터 개수와 같아야 한다.

### 3.10 미리 정의된 함수
입출력 문장이 없으므로 전역 환경에 다음 두 함수가 선언된 것으로 간주한다.
```c
int  input(void)   { ... }  /* 표준 입력에서 정수 하나를 읽어 반환 */
void output(int x) { ... }  /* 정수 x를 표준 출력에 개행과 함께 출력 */
```

---

## 4. 예제 프로그램

### 4.1 최대공약수 (유클리드)
```c
/* A program to perform Euclid's
   Algorithm to compute gcd. */
int gcd (int u, int v)
{ if (v == 0) return u ;
  else return gcd(v,u-u/v*v);
  /* u-u/v*v == u mod v */
}
void main(void)
{ int x; int y;
  x = input(); y = input();
  output(gcd(x,y));
}
```

### 4.2 선택 정렬 (10개 정수)
```c
/* A program to perform selection sort on a 10
   element array. */
int x[10];
int minloc ( int a[], int low, int high )
{ int i; int x; int k;
  k = low;
  x = a[low];
  i = low + 1;
  while (i < high)
    { if (a[i] < x)
        { x = a[i];
          k = i; }
      i = i + 1;
    }
  return k;
}
void sort( int a[], int low, int high)
{ int i; int k;
  i = low;
  while (i < high-1)
    { int t;
      k = minloc(a,i,high);
      t = a[k];
      a[k] = a[i];
      a[i] = t;
      i = i + 1;
    }
}
void main(void)
{ int i;
  i = 0;
  while (i < 10)
    { x[i] = input();
      i = i + 1; }
  sort(x,0,10);
  i = 0;
  while (i < 10)
    { output(x[i]);
      i = i + 1; }
}
```

---

## 5. 구현 시 참고 (원서 외 메모)

| 항목 | 내용 |
| :- | :- |
| 모호성 | dangling else(규칙 15)만 존재. LR 파서에서는 shift 우선으로 해결 |
| LALR(1) | 그 외 충돌 없음 — bison에 그대로 사용 가능 |
| LL(1)/재귀 하강 | 좌재귀(2, 8, 11, 12, 22, 24, 29) 제거 필요. 규칙 18(`var = expr` vs `simple-expression`)과 26(`var` vs `call`)은 ID 다음 토큰(`[`, `(`, `=`)으로 분기 |
| 의미 검사 필수 항목 | void 변수 금지, 선언 전 사용 금지, 마지막 선언 `void main(void)`, 인자 개수/배열 인자 일치, return 타입 일치, 배열은 첨자 없이 사용 금지(배열 인자 전달 제외) |
| 런타임 | 음수 첨자 → 중단, 상한 미검사, `/`는 정수 나눗셈 |
