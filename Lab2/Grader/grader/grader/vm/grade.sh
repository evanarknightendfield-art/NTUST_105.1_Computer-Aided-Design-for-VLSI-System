#!/bin/bash
# grade.sh, 在 VM 內由 grade.service 開機時以 root 執行
# host 已用 virt-copy-in 將以下檔案置於 VM 內:
#   /opt/grader/<SID>.zip     學生繳交的壓縮檔 
#   /opt/grader/inputs/       測資
#   /opt/grader/executor.py
# 跑完後由 grade.service 的 SuccessAction/FailureAction 關機

# 找出 SID: /opt/grader 下唯一的 .zip, 檔名即學號
zips=(/opt/grader/*.zip)
if [ ${#zips[@]} -ne 1 ] || [ ! -f "${zips[0]}" ]; then
    echo "expect exactly one zip in /opt/grader, got: ${zips[*]}"
    exit 1
fi
SID=$(basename "${zips[0]}" .zip)
DEST=/home/grader/$SID
echo "grading $SID"

# 以 grader 身分解壓縮, 學生檔案不經 root 處理
# 正常 zip 內含 <SID>/ 目錄, 若檔案直接放在根目錄也接受
# runuser 指令用於切換使用者權限, 以 grader 一般用戶執行 python 進行解壓縮
TMP=/home/grader/unzip
runuser -u grader -- python3 -m zipfile -e "${zips[0]}" "$TMP" || exit 1
if [ -d "$TMP/$SID" ]; then
    runuser -u grader -- mv "$TMP/$SID" "$DEST"
else
    runuser -u grader -- mv "$TMP" "$DEST"
fi

# 不採用學生自帶的 build/ results/ inputs/, 測資一律用 host 提供的, 因此刪除
rm -rf "$DEST/build" "$DEST/results" "$DEST/inputs"
cp -r /opt/grader/inputs "$DEST/inputs"
chown -R grader:grader "$DEST/inputs"

# 執行 executor
runuser -u grader -- python3 -u /opt/grader/executor.py "$SID"
