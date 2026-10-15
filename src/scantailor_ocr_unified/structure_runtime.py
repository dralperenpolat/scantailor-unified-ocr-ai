"""Runs only in a separately selected PaddleOCR environment."""
import json
import sys

def main():
    request=json.load(sys.stdin)
    from paddleocr import PPStructureV3
    pipeline=PPStructureV3(device='gpu:0' if request['device'].startswith('gpu') else 'cpu',
        use_doc_orientation_classify=False,use_doc_unwarping=False)
    for result in pipeline.predict(input=request['source']):
        result.save_to_json(save_path=request['destination'])
        result.save_to_markdown(save_path=request['destination'])
    return 0

if __name__=='__main__':raise SystemExit(main())
