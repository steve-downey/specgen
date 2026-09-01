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

;;; init.el ends here
