#!/bin/bash
# Auto-update script: runs gen_figures.py and gen_report.py when new cases complete
RESULTS=/workspace/solver/results
REPORT=/workspace/solver/report

check_and_update() {
    NEW_COMPLETED=0
    for CASE in naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 cylinder_m010_laminar_re200 cylinder_m010_laminar_re20; do
        if [ -f "$RESULTS/$CASE/metadata.json" ]; then
            echo "$(date): $CASE completed"
            NEW_COMPLETED=1
        fi
    done
    if [ "$NEW_COMPLETED" -eq 1 ]; then
        echo "$(date): Running gen_figures.py and gen_report.py..."
        cd $REPORT && python3 gen_figures.py 2>&1
        python3 gen_report.py 2>&1
        # Sync CSV from JSON
        python3 -c "
import json, csv
with open('$REPORT/figure_manifest.json') as f:
    entries = json.load(f)
fieldnames = ['figure_file','case_id','figure_type','variable','source_file','caption']
with open('$REPORT/figure_manifest.csv','w',newline='') as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction='ignore')
    writer.writeheader()
    writer.writerows(entries)
print('CSV updated with', len(entries), 'entries')
" 2>&1
    fi
}

# Run loop
while true; do
    check_and_update
    sleep 120
done
