#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/scripts"

python build_all.py
python gen_tests.py --name holdout --count 12

for set in testcases holdout; do
    python run_all.py --set "$set"
    python make_tables.py --set "$set"
done

python make_figures.py --set testcases --trees 001,004
python make_arrival.py --set testcases --case 004
python make_figures.py --set holdout --trees ""
