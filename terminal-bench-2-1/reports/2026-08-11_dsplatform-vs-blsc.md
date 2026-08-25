# Terminal-Bench 2.1 — completed run comparison

Generated 2026-08-11 · terminus-2 · `openai/deepseek-v4-flash` (DSPlatform direct) vs `openai/DeepSeek-V4-Flash` (BLSC direct, config updated after this run)

## Run overview

| Run | Finished | Mean reward | Pass / Fail | Errors | Input | Cache | Output | Cache hit |
|---|---|---|---|---|---|---|---|---|
| DSPlatform 04-25-00 | 2026-08-10T20:29:39.191779 | 0.7865 | 210 / 44 | 13 | 307,911,209 | 297,629,824 | 18,980,451 | 96.7% |
| BLSC 20-52-41 | 2026-08-11T11:30:39.178770 | 0.7228 | 193 / 62 | 12 | 556,552,804 | 0 | 2,960,012 | 0.0% |

Pass/fail counts above exclude errored trials (all `AgentTimeoutError`); errored trials are counted in the Errors column. Both runs are 267 trials total: DSPlatform 210 pass + 44 fail + 13 timeouts; BLSC 193 pass + 62 fail + 12 timeouts. BLSC ran through the litellm proxy and recorded `cached_tokens=0`; its token accounting is not priced here.

## Task completion status (tasks that failed anywhere)

`x/3` = accepted attempts per task (reward 1.0). Attempts are ordered by `started_at`.

| Task | DSPlatform x/3 | DSPlatform failures | BLSC x/3 | BLSC failures |
|---|---|---|---|---|
| bn-fit-modify | 3/3 | — | 2/3 | 1× FAIL |
| break-filter-js-from-html | 3/3 | — | 2/3 | 1× FAIL |
| build-pov-ray | 2/3 | 1× FAIL | 2/3 | 1× FAIL |
| caffe-cifar-10 | 3/3 | — | 2/3 | 1× FAIL |
| cancel-async-tasks | 3/3 | — | 1/3 | 2× FAIL |
| chess-best-move | 2/3 | 1× FAIL | 1/3 | 2× FAIL |
| compile-compcert | 2/3 | 1× FAIL | 3/3 | — |
| crack-7z-hash | 3/3 | — | 2/3 | 1× FAIL |
| db-wal-recovery | 2/3 | 1× FAIL | 0/3 | 3× FAIL |
| dna-assembly | 0/3 | 3× FAIL | 0/3 | 3× FAIL |
| dna-insert | 2/3 | 1× FAIL | 2/3 | 1× FAIL |
| extract-elf | 2/3 | 1× FAIL | 1/3 | 2× FAIL |
| extract-moves-from-video | 0/3 | 3× TIMEOUT 5400.0s | 0/3 | 2× TIMEOUT 5400.0s, 1× FAIL |
| feal-linear-cryptanalysis | 2/3 | 1× TIMEOUT 5400.0s | 3/3 | — |
| filter-js-from-html | 1/3 | 2× FAIL | 0/3 | 3× FAIL |
| financial-document-processor | 3/3 | — | 2/3 | 1× FAIL |
| gcode-to-text | 0/3 | 2× FAIL, 1× TIMEOUT 2700.0s | 0/3 | 2× TIMEOUT 2700.0s, 1× FAIL |
| git-multibranch | 3/3 | — | 2/3 | 1× FAIL |
| gpt2-codegolf | 0/3 | 3× TIMEOUT 2700.0s | 0/3 | 3× TIMEOUT 2700.0s |
| install-windows-3.11 | 2/3 | 1× FAIL | 1/3 | 2× FAIL |
| kv-store-grpc | 2/3 | 1× FAIL | 3/3 | — |
| largest-eigenval | 2/3 | 1× FAIL | 3/3 | — |
| make-doom-for-mips | 0/3 | 2× FAIL, 1× TIMEOUT 2700.0s | 0/3 | 2× FAIL, 1× TIMEOUT 2700.0s |
| make-mips-interpreter | 1/3 | 1× FAIL, 1× TIMEOUT 5400.0s | 0/3 | 2× FAIL, 1× TIMEOUT 5400.0s |
| mcmc-sampling-stan | 2/3 | 1× FAIL | 3/3 | — |
| model-extraction-relu-logits | 0/3 | 3× FAIL | 0/3 | 3× FAIL |
| mteb-leaderboard | 0/3 | 3× FAIL | 1/3 | 2× FAIL |
| nginx-request-logging | 3/3 | — | 2/3 | 1× FAIL |
| overfull-hbox | 1/3 | 2× FAIL | 2/3 | 1× FAIL |
| path-tracing | 1/3 | 2× FAIL | 0/3 | 3× FAIL |
| path-tracing-reverse | 2/3 | 1× TIMEOUT 5400.0s | 2/3 | 1× TIMEOUT 5400.0s |
| protein-assembly | 1/3 | 2× FAIL | 0/3 | 3× FAIL |
| pytorch-model-cli | 3/3 | — | 2/3 | 1× FAIL |
| raman-fitting | 0/3 | 2× FAIL, 1× TIMEOUT 2700.0s | 0/3 | 2× TIMEOUT 2700.0s, 1× FAIL |
| regex-chess | 2/3 | 1× FAIL | 0/3 | 3× FAIL |
| sam-cell-seg | 3/3 | — | 2/3 | 1× FAIL |
| sanitize-git-repo | 3/3 | — | 0/3 | 3× FAIL |
| torch-pipeline-parallelism | 0/3 | 3× FAIL | 0/3 | 3× FAIL |
| torch-tensor-parallelism | 2/3 | 1× FAIL | 3/3 | — |
| train-fasttext | 0/3 | 2× FAIL, 1× TIMEOUT 10800.0s | 0/3 | 3× FAIL |
| video-processing | 0/3 | 3× FAIL | 0/3 | 3× FAIL |

## Price accounting — DSPlatform run (official DeepSeek CNY)

Official `deepseek-v4-flash` off-peak rates used:

- Cache-hit input: ¥0.02 / 1M tokens
- Cache-miss input: ¥1.00 / 1M tokens
- Output: ¥2.00 / 1M tokens

- Input: 307,911,209
- Cache hit: 297,629,824 (96.7%)
- Cache miss: 10,281,385 (3.3%)
- Output: 18,980,451

| Component | Tokens | CNY |
|---|---|---|
| Cache-hit input | 297,629,824 | ¥5.9526 |
| Cache-miss input | 10,281,385 | ¥10.2814 |
| Output | 18,980,451 | ¥37.9609 |
| **Total** | | **¥54.1949** |

## Per-task token accounting — DSPlatform run

| Task | Attempts | Input | Cache hit | Output | Cache hit % | Est. CNY |
|---|---|---|---|---|---|---|
| adaptive-rejection-sampler | 3 | 684,367 | 603,776 | 183,150 | 88.2% | ¥0.4590 |
| bn-fit-modify | 3 | 218,730 | 177,664 | 61,246 | 81.2% | ¥0.1671 |
| break-filter-js-from-html | 3 | 308,401 | 263,296 | 99,447 | 85.4% | ¥0.2493 |
| build-cython-ext | 3 | 3,848,639 | 3,675,392 | 122,337 | 95.5% | ¥0.4914 |
| build-pmars | 3 | 770,858 | 679,680 | 39,307 | 88.2% | ¥0.1834 |
| build-pov-ray | 3 | 4,303,565 | 4,127,360 | 92,834 | 95.9% | ¥0.4444 |
| caffe-cifar-10 | 3 | 5,812,974 | 5,613,184 | 201,751 | 96.6% | ¥0.7156 |
| cancel-async-tasks | 3 | 66,544 | 52,096 | 70,775 | 78.3% | ¥0.1570 |
| chess-best-move | 3 | 3,823,676 | 3,667,456 | 453,648 | 95.9% | ¥1.1369 |
| circuit-fibsqrt | 3 | 38,015,862 | 37,559,296 | 1,023,599 | 98.8% | ¥3.2549 |
| cobol-modernization | 3 | 1,076,712 | 982,656 | 212,933 | 91.3% | ¥0.5396 |
| code-from-image | 3 | 602,475 | 538,880 | 38,292 | 89.4% | ¥0.1510 |
| compile-compcert | 3 | 1,618,522 | 1,508,608 | 67,939 | 93.2% | ¥0.2760 |
| configure-git-webserver | 3 | 450,571 | 392,832 | 94,972 | 87.2% | ¥0.2555 |
| constraints-scheduling | 3 | 42,030 | 27,648 | 42,601 | 65.8% | ¥0.1001 |
| count-dataset-tokens | 3 | 500,494 | 446,208 | 59,734 | 89.2% | ¥0.1827 |
| crack-7z-hash | 3 | 2,360,545 | 2,236,544 | 192,170 | 94.7% | ¥0.5531 |
| custom-memory-heap-crash | 3 | 568,693 | 486,400 | 54,634 | 85.5% | ¥0.2013 |
| db-wal-recovery | 3 | 637,418 | 571,520 | 100,874 | 89.7% | ¥0.2791 |
| distribution-search | 3 | 178,930 | 143,872 | 82,722 | 80.4% | ¥0.2034 |
| dna-assembly | 3 | 2,259,848 | 2,090,624 | 761,064 | 92.5% | ¥1.7332 |
| dna-insert | 3 | 268,748 | 224,640 | 79,389 | 83.6% | ¥0.2074 |
| extract-elf | 3 | 442,440 | 376,448 | 134,204 | 85.1% | ¥0.3419 |
| extract-moves-from-video | 3 | 28,849,721 | 28,461,568 | 698,080 | 98.7% | ¥2.3535 |
| feal-differential-cryptanalysis | 3 | 793,036 | 720,256 | 187,375 | 90.8% | ¥0.4619 |
| feal-linear-cryptanalysis | 3 | 4,465,527 | 4,257,792 | 772,272 | 95.3% | ¥1.8374 |
| filter-js-from-html | 3 | 1,530,019 | 1,416,192 | 294,136 | 92.6% | ¥0.7304 |
| financial-document-processor | 3 | 2,052,891 | 1,936,000 | 86,582 | 94.3% | ¥0.3288 |
| fix-code-vulnerability | 3 | 246,421 | 203,008 | 28,853 | 82.4% | ¥0.1052 |
| fix-git | 3 | 126,230 | 103,424 | 31,626 | 81.9% | ¥0.0881 |
| fix-ocaml-gc | 3 | 7,964,120 | 7,748,736 | 202,252 | 97.3% | ¥0.7749 |
| gcode-to-text | 3 | 12,990,103 | 12,647,040 | 644,872 | 97.4% | ¥1.8857 |
| git-leak-recovery | 3 | 57,034 | 43,136 | 17,619 | 75.6% | ¥0.0500 |
| git-multibranch | 3 | 216,222 | 176,128 | 75,726 | 81.5% | ¥0.1951 |
| gpt2-codegolf | 3 | 8,262,069 | 7,961,088 | 640,240 | 96.4% | ¥1.7407 |
| headless-terminal | 3 | 238,892 | 196,736 | 110,241 | 82.4% | ¥0.2666 |
| hf-model-inference | 3 | 118,570 | 96,000 | 22,830 | 81.0% | ¥0.0702 |
| install-windows-3.11 | 3 | 1,927,816 | 1,788,672 | 191,123 | 92.8% | ¥0.5572 |
| kv-store-grpc | 3 | 55,759 | 43,648 | 19,341 | 78.3% | ¥0.0517 |
| large-scale-text-editing | 3 | 206,002 | 182,912 | 72,032 | 88.8% | ¥0.1708 |
| largest-eigenval | 3 | 2,035,538 | 1,897,216 | 159,238 | 93.2% | ¥0.4947 |
| llm-inference-batching-scheduler | 3 | 1,277,872 | 1,149,440 | 140,035 | 89.9% | ¥0.4315 |
| log-summary-date-ranges | 3 | 87,538 | 65,024 | 11,289 | 74.3% | ¥0.0464 |
| mailman | 3 | 2,498,520 | 2,372,352 | 207,147 | 95.0% | ¥0.5879 |
| make-doom-for-mips | 3 | 14,424,649 | 14,061,696 | 545,906 | 97.5% | ¥1.7360 |
| make-mips-interpreter | 3 | 23,701,490 | 23,231,872 | 880,540 | 98.0% | ¥2.6953 |
| mcmc-sampling-stan | 3 | 1,830,638 | 1,712,896 | 114,989 | 93.6% | ¥0.3820 |
| merge-diff-arc-agi-task | 3 | 544,158 | 491,392 | 107,558 | 90.3% | ¥0.2777 |
| model-extraction-relu-logits | 3 | 35,056 | 24,576 | 25,569 | 70.1% | ¥0.0621 |
| modernize-scientific-stack | 3 | 53,465 | 40,960 | 17,863 | 76.6% | ¥0.0491 |
| mteb-leaderboard | 3 | 2,718,484 | 2,563,456 | 88,961 | 94.3% | ¥0.3842 |
| mteb-retrieve | 3 | 370,782 | 324,480 | 32,163 | 87.5% | ¥0.1171 |
| multi-source-data-merger | 3 | 93,687 | 71,552 | 34,287 | 76.4% | ¥0.0921 |
| nginx-request-logging | 3 | 103,495 | 80,640 | 63,959 | 77.9% | ¥0.1524 |
| openssl-selfsigned-cert | 3 | 96,582 | 74,752 | 31,302 | 77.4% | ¥0.0859 |
| overfull-hbox | 3 | 275,382 | 233,728 | 48,295 | 84.9% | ¥0.1429 |
| password-recovery | 3 | 271,929 | 226,688 | 63,494 | 83.4% | ¥0.1768 |
| path-tracing | 3 | 16,330,168 | 15,986,176 | 692,056 | 97.9% | ¥2.0478 |
| path-tracing-reverse | 3 | 18,509,096 | 18,107,520 | 1,392,552 | 97.8% | ¥3.5488 |
| polyglot-c-py | 3 | 202,818 | 165,120 | 157,606 | 81.4% | ¥0.3562 |
| polyglot-rust-c | 3 | 196,233 | 166,784 | 232,009 | 85.0% | ¥0.4968 |
| portfolio-optimization | 3 | 458,305 | 401,664 | 75,215 | 87.6% | ¥0.2151 |
| protein-assembly | 3 | 2,078,633 | 1,921,024 | 350,104 | 92.4% | ¥0.8962 |
| prove-plus-comm | 3 | 46,532 | 37,120 | 24,252 | 79.8% | ¥0.0587 |
| pypi-server | 3 | 72,797 | 54,272 | 28,151 | 74.6% | ¥0.0759 |
| pytorch-model-cli | 3 | 520,847 | 452,992 | 64,806 | 87.0% | ¥0.2065 |
| pytorch-model-recovery | 3 | 1,065,754 | 990,464 | 114,912 | 92.9% | ¥0.3249 |
| qemu-alpine-ssh | 3 | 2,238,104 | 2,125,696 | 213,697 | 95.0% | ¥0.5823 |
| qemu-startup | 3 | 2,064,897 | 1,958,784 | 139,116 | 94.9% | ¥0.4235 |
| query-optimize | 3 | 271,145 | 233,216 | 65,960 | 86.0% | ¥0.1745 |
| raman-fitting | 3 | 6,991,497 | 6,717,952 | 688,713 | 96.1% | ¥1.7853 |
| regex-chess | 3 | 13,663,110 | 13,289,088 | 541,704 | 97.3% | ¥1.7232 |
| regex-log | 3 | 59,076 | 40,832 | 88,026 | 69.1% | ¥0.1951 |
| reshard-c4-data | 3 | 396,178 | 340,480 | 165,907 | 85.9% | ¥0.3943 |
| rstan-to-pystan | 3 | 3,808,117 | 3,644,928 | 107,633 | 95.7% | ¥0.4514 |
| sam-cell-seg | 3 | 3,137,647 | 2,993,024 | 284,693 | 95.4% | ¥0.7739 |
| sanitize-git-repo | 3 | 1,206,692 | 1,099,648 | 126,775 | 91.1% | ¥0.3826 |
| schemelike-metacircular-eval | 3 | 2,176,181 | 2,055,424 | 336,055 | 94.5% | ¥0.8340 |
| sparql-university | 3 | 208,764 | 166,400 | 76,033 | 79.7% | ¥0.1978 |
| sqlite-db-truncate | 3 | 168,947 | 125,184 | 76,404 | 74.1% | ¥0.1991 |
| sqlite-with-gcov | 3 | 783,326 | 705,920 | 39,911 | 90.1% | ¥0.1713 |
| torch-pipeline-parallelism | 3 | 176,146 | 142,720 | 234,917 | 81.0% | ¥0.5061 |
| torch-tensor-parallelism | 3 | 156,334 | 127,488 | 202,540 | 81.5% | ¥0.4365 |
| train-fasttext | 3 | 16,637,257 | 16,393,856 | 484,146 | 98.5% | ¥1.5396 |
| tune-mjcf | 3 | 486,989 | 439,552 | 48,175 | 90.3% | ¥0.1526 |
| video-processing | 3 | 7,134,762 | 6,880,768 | 298,731 | 96.4% | ¥0.9891 |
| vulnerable-secret | 3 | 100,531 | 73,344 | 21,516 | 73.0% | ¥0.0717 |
| winning-avg-corewars | 3 | 16,390,686 | 16,031,872 | 449,117 | 97.8% | ¥1.5777 |
| write-compressor | 3 | 792,901 | 709,376 | 221,602 | 89.5% | ¥0.5409 |
| **Total** | | **307,911,209** | **297,629,824** | **18,980,451** | **96.7%** | **¥54.1949** |
