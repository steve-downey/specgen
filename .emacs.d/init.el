;; Minimal batch export setup for repository org examples. -*- lexical-binding: t -*-
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

(setq custom-file (locate-user-emacs-file "custom.el"))

(require 'package)

(add-to-list 'package-archives '("gnu" . "https://elpa.gnu.org/packages/") t)
(add-to-list 'package-archives '("melpa" . "https://melpa.org/packages/") t)

(setq package-user-dir
      (locate-user-emacs-file (concat "elpa-" emacs-version)))

(package-initialize)

(unless package-archive-contents
  (package-refresh-contents))

(dolist (pkg '(htmlize org-transclusion ox-gfm))
  (unless (package-installed-p pkg)
    (package-install pkg)))

(require 'org)
(require 'htmlize)
(require 'org-transclusion)
(require 'ox-gfm)

;; The live example document (docs/examples-live.org) runs its shell blocks at
;; export time, so ob-shell has to be loaded: without it org exports the source
;; and silently produces no results, which looks like a document that simply
;; has no output. Evaluation is unconfirmed because batch export cannot answer
;; a prompt.
(org-babel-do-load-languages 'org-babel-load-languages '((shell . t)))
(setq org-confirm-babel-evaluate nil)
(setq org-export-with-toc t)
(setq org-export-headline-levels 8)
(setq org-html-htmlize-output-type 'css)
(setq org-src-fontify-natively t)
(setq org-src-preserve-indentation t)
(setq org-startup-folded nil)
(setq org-startup-truncated nil)
;; A capture is shown byte for byte. With tabs allowed, org re-indents a
;; source block's lines on export and turns each leading run of eight spaces
;; into a tab, which rewrote the help text and the JSON this document embeds.
(setq-default indent-tabs-mode nil)

;; ox-gfm always fences a source block with three backticks. The transcluded
;; mpark outputs are markdown that carries its own ``` fences, so the outer
;; block closed at the first inner fence and the rest of the output spilled
;; into the document as prose. CommonMark lets a fence be any run of three or
;; more backticks and closes it only with a run at least that long, so size
;; each fence to one more than the longest run its body contains.
(defun specgen--gfm-src-block (src-block _contents info)
  "Transcode SRC-BLOCK to GFM with a fence longer than any run it contains."
  (let* ((lang (org-element-property :language src-block))
         (code (org-export-format-code-default src-block info))
         (longest 0)
         (start 0))
    (while (string-match "`+" code start)
      (setq longest (max longest (- (match-end 0) (match-beginning 0)))
            start (match-end 0)))
    (let ((fence (make-string (max 3 (1+ longest)) ?`)))
      (concat fence lang "\n" code fence))))

(advice-add 'org-gfm-src-block :override #'specgen--gfm-src-block)

;; A transcluded capture is source to show, not org to interpret. Inside a
;; source block org takes a line that starts with `*' as a headline and one
;; that starts with `#+' as a keyword unless it is comma-escaped, and
;; org-transclusion wraps the file's text in the block as it is. The org
;; captures have both, so their block ended at the first headline, the rest of
;; the capture was exported as if it were part of this document, and once a
;; capture carried wg21org's `[[delete:][...]]' change links the export
;; stopped altogether on a link it could not resolve (issue #294). Escaping
;; the text on its way in keeps the block whole; org removes the commas again
;; when it exports a source block, so what is shown is the capture's own text.
(defun specgen--escape-transcluded-source (payload)
  "Comma-escape the :src-content of PAYLOAD, a transclusion's content plist."
  (let ((content (plist-get payload :src-content)))
    (if (stringp content)
        (plist-put payload :src-content (org-escape-code-in-string content))
      payload)))

(advice-add 'org-transclusion-content-range-of-lines
            :filter-return #'specgen--escape-transcluded-source)

;;; init.el ends here
